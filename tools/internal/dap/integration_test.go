package dap

import (
	"bufio"
	"encoding/json"
	"io"
	"log"
	"os"
	"path/filepath"
	"testing"
	"time"
)

type wireMessage struct {
	Type    string          `json:"type"`
	Command string          `json:"command,omitempty"`
	Event   string          `json:"event,omitempty"`
	Success bool            `json:"success,omitempty"`
	Body    json.RawMessage `json:"body,omitempty"`
}

func TestDAPEndToEnd(t *testing.T) {
	runtimeExecutable := os.Getenv("TOY_DAP_RUNTIME")
	if runtimeExecutable == "" {
		t.Skip("TOY_DAP_RUNTIME is not set")
	}
	runtimeExecutable, err := filepath.Abs(runtimeExecutable)
	if err != nil {
		t.Fatal(err)
	}
	program, err := filepath.Abs(filepath.Join("..", "..", "..",
		"tests", "toy", "test_debug_protocol.toy"))
	if err != nil {
		t.Fatal(err)
	}

	clientInput, serverInput := io.Pipe()
	serverOutput, clientOutput := io.Pipe()
	server := NewServer(log.New(io.Discard, "", 0))
	serverDone := make(chan error, 1)
	go func() {
		serverDone <- server.Serve(clientInput, clientOutput)
	}()
	reader := bufio.NewReader(serverOutput)
	sequence := 0

	send := func(command string, arguments any) {
		t.Helper()
		sequence++
		request := map[string]any{
			"seq":     sequence,
			"type":    "request",
			"command": command,
		}
		if arguments != nil {
			request["arguments"] = arguments
		}
		if _, err := serverInput.Write(frameMessage(t, request)); err != nil {
			t.Fatal(err)
		}
	}
	read := func() wireMessage {
		t.Helper()
		type result struct {
			message wireMessage
			err     error
		}
		resultChannel := make(chan result, 1)
		go func() {
			payload, err := readMessage(reader)
			if err != nil {
				resultChannel <- result{err: err}
				return
			}
			var message wireMessage
			err = json.Unmarshal(payload, &message)
			resultChannel <- result{message: message, err: err}
		}()
		select {
		case result := <-resultChannel:
			if result.err != nil {
				t.Fatal(result.err)
			}
			return result.message
		case <-time.After(5 * time.Second):
			t.Fatal("timed out waiting for DAP message")
			return wireMessage{}
		}
	}
	expect := func(messageType, name string) wireMessage {
		t.Helper()
		message := read()
		actual := message.Command
		if message.Type == "event" {
			actual = message.Event
		}
		if message.Type != messageType || actual != name {
			t.Fatalf("got %s %s, want %s %s", message.Type, actual,
				messageType, name)
		}
		if message.Type == "response" && !message.Success {
			t.Fatalf("%s request failed", name)
		}
		return message
	}

	send("initialize", map[string]any{"adapterID": "toy"})
	expect("response", "initialize")
	send("launch", map[string]any{
		"program":           program,
		"runtimeExecutable": runtimeExecutable,
		"cwd":               filepath.Dir(program),
		"args":              []string{"--debuggee-option"},
		"stopOnEntry":       true,
	})
	expect("response", "launch")
	expect("event", "initialized")
	send("setBreakpoints", map[string]any{
		"source":      map[string]any{"path": program},
		"breakpoints": []any{map[string]any{"line": 4}},
	})
	expect("response", "setBreakpoints")
	send("configurationDone", map[string]any{})
	expect("response", "configurationDone")
	entry := expect("event", "stopped")
	var entryBody struct {
		Reason string `json:"reason"`
	}
	if err := json.Unmarshal(entry.Body, &entryBody); err != nil {
		t.Fatal(err)
	}
	if entryBody.Reason != "entry" {
		t.Fatalf("initial stop reason = %q, want entry", entryBody.Reason)
	}
	send("threads", nil)
	expect("response", "threads")
	send("stackTrace", map[string]any{"threadId": toyThreadID})
	stackTrace := expect("response", "stackTrace")
	var stackTraceBody struct {
		StackFrames []struct {
			ID   int `json:"id"`
			Line int `json:"line"`
		} `json:"stackFrames"`
	}
	if err := json.Unmarshal(stackTrace.Body, &stackTraceBody); err != nil {
		t.Fatal(err)
	}
	if len(stackTraceBody.StackFrames) == 0 || stackTraceBody.StackFrames[0].Line != 2 {
		t.Fatalf("unexpected entry frames: %#v", stackTraceBody.StackFrames)
	}
	send("scopes", map[string]any{"frameId": stackTraceBody.StackFrames[0].ID})
	scopes := expect("response", "scopes")
	var scopesBody struct {
		Scopes []struct {
			Name               string `json:"name"`
			VariablesReference int    `json:"variablesReference"`
		} `json:"scopes"`
	}
	if err := json.Unmarshal(scopes.Body, &scopesBody); err != nil {
		t.Fatal(err)
	}
	if len(scopesBody.Scopes) != 1 ||
		scopesBody.Scopes[0].VariablesReference != dataStackReference {
		t.Fatalf("unexpected scopes: %#v", scopesBody.Scopes)
	}

	send("continue", map[string]any{"threadId": toyThreadID})
	expect("response", "continue")
	expect("event", "continued")
	stopped := expect("event", "stopped")
	var stoppedBody struct {
		Reason string `json:"reason"`
	}
	if err := json.Unmarshal(stopped.Body, &stoppedBody); err != nil {
		t.Fatal(err)
	}
	if stoppedBody.Reason != "breakpoint" {
		t.Fatalf("stop reason = %q, want breakpoint", stoppedBody.Reason)
	}
	send("stackTrace", map[string]any{"threadId": toyThreadID})
	stackTrace = expect("response", "stackTrace")
	if err := json.Unmarshal(stackTrace.Body, &stackTraceBody); err != nil {
		t.Fatal(err)
	}
	if len(stackTraceBody.StackFrames) == 0 {
		t.Fatal("breakpoint stop has no stack frame")
	}
	send("scopes", map[string]any{
		"frameId": stackTraceBody.StackFrames[0].ID,
	})
	scopes = expect("response", "scopes")
	if err := json.Unmarshal(scopes.Body, &scopesBody); err != nil {
		t.Fatal(err)
	}
	capturesReference := 0
	for _, scope := range scopesBody.Scopes {
		if scope.Name == "Captures" {
			capturesReference = scope.VariablesReference
			break
		}
	}
	if capturesReference == 0 {
		t.Fatalf("capture scope missing: %#v", scopesBody.Scopes)
	}
	send("variables", map[string]any{
		"variablesReference": capturesReference,
	})
	captures := expect("response", "variables")
	var capturesBody struct {
		Variables []struct {
			Name  string `json:"name"`
			Value string `json:"value"`
		} `json:"variables"`
	}
	if err := json.Unmarshal(captures.Body, &capturesBody); err != nil {
		t.Fatal(err)
	}
	if len(capturesBody.Variables) != 1 ||
		capturesBody.Variables[0].Name != "seed" ||
		capturesBody.Variables[0].Value != "7" {
		t.Fatalf("unexpected captures: %#v", capturesBody.Variables)
	}

	send("stepIn", map[string]any{"threadId": toyThreadID})
	expect("response", "stepIn")
	expect("event", "continued")
	expect("event", "stopped")
	send("variables", map[string]any{"variablesReference": dataStackReference})
	variables := expect("response", "variables")
	var variablesBody struct {
		Variables []struct {
			Value string `json:"value"`
		} `json:"variables"`
	}
	if err := json.Unmarshal(variables.Body, &variablesBody); err != nil {
		t.Fatal(err)
	}
	if len(variablesBody.Variables) == 0 || variablesBody.Variables[0].Value != "0" {
		t.Fatalf("unexpected data stack: %#v", variablesBody.Variables)
	}

	send("continue", map[string]any{"threadId": toyThreadID})
	expect("response", "continue")
	expect("event", "continued")
	expect("event", "exited")
	expect("event", "terminated")
	send("disconnect", map[string]any{})
	expect("response", "disconnect")
	_ = serverInput.Close()
	_ = serverOutput.Close()
	select {
	case err := <-serverDone:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("DAP server did not exit after disconnect")
	}
}
