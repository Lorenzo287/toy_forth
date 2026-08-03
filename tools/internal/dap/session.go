package dap

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

const machineRecordSeparator = byte(0x1e)

type machineFrame struct {
	ID       int            `json:"id"`
	Name     string         `json:"name"`
	Source   string         `json:"source"`
	Line     int            `json:"line"`
	Column   int            `json:"column"`
	PC       int            `json:"pc"`
	Length   int            `json:"length"`
	Captures []machineValue `json:"captures,omitempty"`
}

type machineValue struct {
	Name  string `json:"name"`
	Type  string `json:"type"`
	Value string `json:"value"`
}

type machineEvent struct {
	Event    string         `json:"event"`
	Reason   string         `json:"reason,omitempty"`
	Message  string         `json:"message,omitempty"`
	Source   string         `json:"source,omitempty"`
	Line     int            `json:"line,omitempty"`
	Column   int            `json:"column,omitempty"`
	Frames   []machineFrame `json:"frames,omitempty"`
	Stack    []machineValue `json:"stack,omitempty"`
	ExitCode int            `json:"exitCode,omitempty"`
}

type processOutput struct {
	Category string
	Text     string
}

type sessionConfig struct {
	RuntimeExecutable string
	Program           string
	Cwd               string
	Args              []string
}

type debugSession struct {
	cmd     *exec.Cmd
	stdin   io.WriteCloser
	events  chan machineEvent
	outputs chan processOutput
	writeMu sync.Mutex
	killMu  sync.Mutex
	killed  bool
	program string
}

func startDebugSession(config sessionConfig) (*debugSession, machineEvent, error) {
	program, err := filepath.Abs(config.Program)
	if err != nil {
		return nil, machineEvent{}, fmt.Errorf("resolve program: %w", err)
	}
	runtimeExecutable := config.RuntimeExecutable
	if runtimeExecutable == "" {
		return nil, machineEvent{}, fmt.Errorf("runtimeExecutable is required")
	}

	args := []string{"--debug-protocol", "--file", program}
	if len(config.Args) > 0 {
		args = append(args, "--")
		args = append(args, config.Args...)
	}
	cmd := exec.Command(runtimeExecutable, args...)
	cmd.Dir = config.Cwd
	if cmd.Dir == "" {
		cmd.Dir = filepath.Dir(program)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, machineEvent{}, err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return nil, machineEvent{}, err
	}
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, machineEvent{}, err
	}
	if err := cmd.Start(); err != nil {
		return nil, machineEvent{}, err
	}

	session := &debugSession{
		cmd:     cmd,
		stdin:   stdin,
		events:  make(chan machineEvent, 16),
		outputs: make(chan processOutput, 32),
		program: program,
	}
	waitCode := make(chan int, 1)
	go func() {
		err := cmd.Wait()
		code := 0
		if err != nil {
			if exitErr, ok := err.(*exec.ExitError); ok {
				code = exitErr.ExitCode()
			} else {
				code = 1
			}
		}
		waitCode <- code
	}()
	go session.readMachineStream(stdout, waitCode)
	go session.readOutput(stderr, "stderr")

	select {
	case first, ok := <-session.events:
		if !ok {
			return nil, machineEvent{}, fmt.Errorf("Toy exited before its first debug event")
		}
		if first.Event != "stopped" {
			_ = session.kill()
			return nil, machineEvent{}, fmt.Errorf("expected initial stopped event, got %q", first.Event)
		}
		return session, first, nil
	case <-time.After(5 * time.Second):
		_ = session.kill()
		return nil, machineEvent{}, fmt.Errorf("timed out waiting for Toy to stop")
	}
}

func (s *debugSession) command(command string) error {
	s.writeMu.Lock()
	defer s.writeMu.Unlock()
	_, err := fmt.Fprintln(s.stdin, command)
	return err
}

func (s *debugSession) kill() error {
	s.killMu.Lock()
	defer s.killMu.Unlock()
	if s.killed || s.cmd.Process == nil {
		return nil
	}
	s.killed = true
	return s.cmd.Process.Kill()
}

func (s *debugSession) readMachineStream(reader io.Reader, waitCode <-chan int) {
	defer close(s.events)
	buffered := bufio.NewReader(reader)
	var output strings.Builder
	sawTermination := false

	flushOutput := func() {
		if output.Len() == 0 {
			return
		}
		s.events <- machineEvent{Event: "output", Message: output.String()}
		output.Reset()
	}

	for {
		value, err := buffered.ReadByte()
		if err != nil {
			flushOutput()
			break
		}
		if value != machineRecordSeparator {
			output.WriteByte(value)
			if value == '\n' {
				flushOutput()
			}
			continue
		}
		flushOutput()
		line, err := buffered.ReadString('\n')
		if err != nil && len(line) == 0 {
			break
		}
		var event machineEvent
		if decodeErr := json.Unmarshal([]byte(strings.TrimSpace(line)), &event); decodeErr != nil {
			s.events <- machineEvent{Event: "error", Message: decodeErr.Error()}
			continue
		}
		if event.Event == "terminated" {
			sawTermination = true
		}
		s.events <- event
	}

	exitCode := <-waitCode
	if !sawTermination {
		s.events <- machineEvent{Event: "terminated", ExitCode: exitCode}
	}
}

func (s *debugSession) readOutput(reader io.Reader, category string) {
	defer close(s.outputs)
	buffered := bufio.NewReader(reader)
	for {
		text, err := buffered.ReadString('\n')
		if text != "" {
			s.outputs <- processOutput{Category: category, Text: text}
		}
		if err != nil {
			return
		}
	}
}
