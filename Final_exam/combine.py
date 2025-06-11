import tkinter as tk
from tkinter import messagebox
import subprocess
import os

# Set file names or full paths if not in the same directory
FILES = {
    "Start UDP Server": "G:\ssssssss\Year3\RUPP\semester2\programming_in_networking\Final_exam\server_udp.exe",
    "Start TCP Server": "G:\ssssssss\Year3\RUPP\semester2\programming_in_networking\Final_exam\server_tcp.exe",
    "Start UDP Client": "G:\ssssssss\Year3\RUPP\semester2\programming_in_networking\Final_exam\client_udp.exe",
    "Start TCP Client": "G:\ssssssss\Year3\RUPP\semester2\programming_in_networking\Final_exam\client_tcp.exe",
}

def run_exe(name):
    exe_path = FILES[name]
    if not os.path.exists(exe_path):
        messagebox.showerror("Error", f"'{exe_path}' not found.")
        return
    try:
        subprocess.Popen(exe_path)
        messagebox.showinfo("Success", f"{name} launched.")
    except Exception as e:
        messagebox.showerror("Error", f"Failed to launch {name}:\n{e}")

# GUI Setup
root = tk.Tk()
root.title("UDP/TCP Server & Client Launcher")
root.geometry("300x300")

tk.Label(root, text="Select to Launch", font=("Arial", 14)).pack(pady=10)

for name in FILES:
    tk.Button(root, text=name, width=25, command=lambda n=name: run_exe(n)).pack(pady=5)

root.mainloop()
