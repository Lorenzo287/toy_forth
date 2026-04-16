import subprocess
import sys
import os

def run_test():
    exe = os.path.join("build", "toy_forth.exe")
    test_file = os.path.join("fth", "test_master.fth")
    
    if not os.path.exists(exe):
        print(f"Error: {exe} not found. Please build the project first.")
        return

    print("Running Toy Forth Test Suite...")
    print("-" * 40)

    try:
        result = subprocess.run([exe, test_file], capture_output=True, text=True, check=True)
        output = result.stdout
        
        expected_outputs = [
            ("Math: 1 + 2 = 3", "3"),
            ("Math: 10 - 4 = 6", "6"),
            ("Math: 3 * 4 = 12", "12"),
            ("Math: 10 / 2 = 5", "5"),
            ("Math: Floats 1.5 + 2.0 = 3.5", "3.5"),
            ("Stack: dup (5 -> 5 5)", "10"),
            ("Stack: swap (1 2 -> 2 1)", "1"),
            ("Comp: 1 < 2 is true", "true"),
            ("Comp: 10 == 10 is true", "true"),
            ("Control: ifelse (true)", "ok"),
            ("Control: while (countdown 3 to 1)", "3\n2\n1"),
            ("Define: colon (square 4 = 16)", "16"),
            ("Define: functional (cube 3 = 27)", "27"),
        ]

        passed = 0
        total = len(expected_outputs)

        # Split output into lines and remove the prefix "Tokenized program..." and "Stack content..."
        lines = [line.strip() for line in output.split('\n') if line.strip()]
        
        # We start looking for results after the first print
        for label, expected in expected_outputs:
            label_index = -1
            for i, line in enumerate(lines):
                if line == label: # Match exact label
                    label_index = i
                    break
            
            if label_index != -1 and label_index + 1 < len(lines):
                # The result is the line(s) following the label
                actual_line = lines[label_index + 1]
                
                if "\n" in expected: # Multiline (while loop)
                    expected_lines = expected.split("\n")
                    actual_lines = lines[label_index+1 : label_index+1+len(expected_lines)]
                    if expected_lines == actual_lines:
                        print(f"[PASS] {label}")
                        passed += 1
                    else:
                        print(f"[FAIL] {label}")
                        print(f"       Expected: {expected_lines}")
                        print(f"       Actual:   {actual_lines}")
                else: # Single line
                    if expected == actual_line:
                        print(f"[PASS] {label}")
                        passed += 1
                    else:
                        print(f"[FAIL] {label}")
                        print(f"       Expected: {expected}")
                        print(f"       Actual:   {actual_line}")
            else:
                print(f"[ERR ] Could not find test label or output for: {label}")

        print("-" * 40)
        print(f"Summary: {passed}/{total} tests passed.")
        
        if passed == total:
            print("OVERALL STATUS: SUCCESS")
            sys.exit(0)
        else:
            print("OVERALL STATUS: FAILURE")
            sys.exit(1)

    except subprocess.CalledProcessError as e:
        print("Interpreter crashed or returned error:")
        print("STDOUT:", e.stdout)
        print("STDERR:", e.stderr)
        sys.exit(1)

if __name__ == "__main__":
    run_test()
