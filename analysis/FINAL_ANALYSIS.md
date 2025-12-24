# Final WMI Capture Analysis - PHN16-72

## Captured Data Summary
Date: 2025-12-23  
Modes captured: QUIET, BALANCED, PERFORMANCE, TURBO (AC plugged in)

---

## Key Findings

### 1. Platform Profile (0x000B) - WORKS AS EXPECTED ✅
| Mode | Value |
|------|-------|
| QUIET | 0x00 |
| BALANCED | 0x01 |
| PERFORMANCE | 0x04 |
| TURBO | 0x05 |

This is the ONLY setting that changes between modes!

### 2. OC Settings (0x0005, 0x0007) - ALWAYS 0xFF
| Setting | QUIET | BALANCED | PERFORMANCE | TURBO |
|---------|-------|----------|-------------|-------|
| 0x0005 (OC_1) | 0xFF | 0xFF | 0xFF | 0xFF |
| 0x0007 (OC_2) | 0xFF | 0xFF | 0xFF | 0xFF |

**Interpretation:** 0xFF typically means "not supported" or "not readable". These settings may be:
- Write-only (can set but not read)
- Not used on PHN16-72 (different mechanism)
- Controlled by a different method (SetCPUOverclockingProfile?)

### 3. Other Settings - NO CHANGE
| Setting | Value (all modes) | Possible Meaning |
|---------|-------------------|------------------|
| 0x0002 | 0x02 | Unknown |
| 0x0006 | 0x00 | Unknown |
| 0x0008 | 0x01 | Unknown |
| 0x0009 | 0x07 | Unknown |
| 0x000A | 0x73 | Supported profiles bitmask (binary: 01110011) |
| 0x000C | 0x00 | Unknown |
| 0x000F | 0x01 | Unknown |

### 4. ProfileSettings - NO CHANGE
All ProfileSettings values are identical across modes.

### 5. LED Behavior - NO CHANGE
LED settings are identical across all modes.

---

## 0x000A Decoded: Supported Profiles
Value: 0x73 = binary 01110011

| Bit | Value | Profile |
|-----|-------|---------|
| 0 | 1 | QUIET (0x00) ✓ |
| 1 | 1 | BALANCED (0x01) ✓ |
| 2 | 0 | (0x02) ✗ |
| 3 | 0 | (0x03) ✗ |
| 4 | 1 | PERFORMANCE (0x04) ✓ |
| 5 | 1 | TURBO (0x05) ✓ |
| 6 | 1 | ECO (0x06) ✓ (battery only) |

---

## Critical Insight

**The WMI Get methods don't show the actual TDP/fan settings!**

What Windows Predator Sense likely does:
1. Calls `SetGamingMiscSetting(0x000B, profile)` to set platform profile
2. The BIOS/EC automatically adjusts TDP, fan curves, etc. based on the profile
3. No separate OC calls are needed - the profile change triggers internal firmware changes

**This matches the observed behavior:** On Linux, setting 0x000B changes the profile indicator, but the actual power limits don't change because something else is missing.

---

## Theory: What Linux Driver Is Missing

1. **Not using the correct WMI method** - Maybe needs to use `SetGamingProfile()` instead of `SetGamingMiscSetting()`
2. **Missing initialization call** - Windows might send an init command at startup
3. **Missing confirmation/commit call** - Profile change might need a follow-up call
4. **EC not being notified** - The EC might need a separate poke to apply changes

---

## Next Steps

1. **Try using SetGamingProfile() instead** - The Windows WMI has this method
2. **Capture with ETW during mode switch** - See the exact SEQUENCE of calls
3. **Check if any Acer services are involved** - They might be doing additional work

---

## Raw Data Locations
- `complete_capture_20251223_220129/state_dump_QUIET.txt`
- `complete_capture_20251223_220129/state_dump_BALANCED.txt`
- `complete_capture_20251223_220129/state_dump_PERFORMANCE.txt`
- `complete_capture_20251223_220129/state_dump_TURBO.txt`
