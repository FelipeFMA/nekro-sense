
import re
import sys

def strings(filename, min=4):
    with open(filename, "rb") as f:
        result = ""
        for c in f.read():
            c = chr(c)
            if c in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_- ":
                result += c
            else:
                if len(result) >= min:
                    yield result
                result = ""

def analyze_binary(path):
    print(f"--- Strings in {path} ---")
    wmi_keywords = [
        "Init", "Reset", "Gaming", "Zone", "Profile", "Color", 
        "Set", "Get", "Method", "WMI", "Acer", "Predator", 
        "6AF4F258", "95764E09", "61EF69EA", "7A4DDFE7", "79772EC5" # GUIDs
    ]
    
    count = 0
    for s in strings(path):
        if any(k in s for k in wmi_keywords):
            print(s)
            count += 1
            if count > 100: # Limit output
                print("... (truncated)")
                break

if __name__ == "__main__":
    analyze_binary(r"C:\Users\felip\Downloads\nekro-sense\predatorservice.inf_amd64_ec38587b71ef8108\AcerLightingService.exe")
    analyze_binary(r"C:\Users\felip\Downloads\nekro-sense\predatorservice.inf_amd64_ec38587b71ef8108\AcerECKeyboardController.dll")
