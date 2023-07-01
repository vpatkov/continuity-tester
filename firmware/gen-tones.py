import sys
import math

F_CPU = float(sys.argv[1])
HIGHEST_TONE = 2959.96
NR_STEPS = 10
POWER_UP_SOUND = [9, 4, 0]
POWER_DOWN_SOUND = [0, 4, 9]
ERROR_SOUND = [0, 0]

steps = [HIGHEST_TONE / 2**(i/12) for i in range(NR_STEPS)]
ocrs = [round(F_CPU/f)-1 for f in steps]
if not all(ocr in range(1, 0x10000) for ocr in ocrs):
    sys.exit(f"OCR overflow: {ocrs}")
cents = [1200 * math.log2(F_CPU/(ocrs[i]+1)/steps[i]) for i in range(NR_STEPS)]
if not all(abs(c) < 10 for c in cents):
    sys.exit(f"Not in tune: {cents}")

r_tones = []
for adc in range(256):
    r = int(adc*1.24/256*10)
    if r < NR_STEPS:
        r_tones.append(ocrs[r])
    else:
        r_tones.append(0)

print("/* Auto-generated. Do not edit. */\n")
print("#ifndef TONES_HPP_")
print("#define TONES_HPP_\n")
print("#include <stdint.h>")
print("#include <avr/pgmspace.h>\n")
print("/* Map ADC -> OCR value or 0 for off */")
print("constexpr uint16_t r_tones[256] PROGMEM = {", end="")
for i, x in enumerate(r_tones):
    if i % 10 == 0:
        print("\n\t", end="")
    print(f"{x:5},", end="")
print("\n};\n")
print("/* OCR values here */")
print("constexpr uint16_t power_up_sound[] PROGMEM = {", end="")
print(", ".join(f"{ocrs[i]}" for i in POWER_UP_SOUND), end=", 0")
print("};")
print("constexpr uint16_t power_down_sound[] PROGMEM = {", end="")
print(", ".join(f"{ocrs[i]}" for i in POWER_DOWN_SOUND), end=", 0")
print("};")
print("constexpr uint16_t error_sound[] PROGMEM = {", end="")
print(", ".join(f"{ocrs[i]}" for i in ERROR_SOUND), end=", 0")
print("};\n")
print("#endif")
