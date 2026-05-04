import os
import subprocess
import sys


def setup():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    venv_dir = os.path.join(script_dir, "venv")
    req_file = os.path.join(script_dir, "requirements.txt")
    
    print(f"--- Creating virtual environment in: {venv_dir} ---")
    subprocess.check_call([sys.executable, "-m", "venv", venv_dir])

    if os.name == "nt":  # Windows
        python_exe = os.path.join(venv_dir, "Scripts", "python.exe")
        activate_cmd = f"venv\\Scripts\\activate"
    else:  # Linux / macOS
        python_exe = os.path.join(venv_dir, "bin", "python")
        activate_cmd = f"source venv/bin/activate"

    print("--- Upgrading pip ---")
    subprocess.check_call([python_exe, "-m", "pip", "install", "--upgrade", "pip"])

    if os.path.exists(req_file):
        print(f"--- Installing requirements ---")
        subprocess.check_call([python_exe, "-m", "pip", "install", "-r", req_file])
    else:
        print(f"--- No requirements.txt found, skipping ---")

    print("\n" + "="*40)
    print("Setup Complete!")
    print(f"To finish activation, run: {activate_cmd}")
    print("="*40)




if __name__ == "__main__":
    setup()