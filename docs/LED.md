# LED.md

These 5mm LED beads, equipped with a clear lens, emit bright light, making them suitable as indicator lights in applications requiring reliable LEDs, such as electronic components, PCB circuits, and DIY light bulb projects. 

They can also be used to make LED glow sticks.

Light Color: RGB Common Anode; 

Rating Voltage: 3-12V; 

Head Size: 5 x 8.7mm / 0.2 x 0.34 Inch / Pitch 2.54mm; 

Total Length: 5mm / 0.20 Inch / Needle Length: 26mm / 1.02 Inch; Resistor Dimensions: 2.2 x 6mm / 0.09 x 0.24 Inch (W x L) / Single-Sided Pin Length: 26mm / 1.02 Inch; Packing List: 50pcs x  LED Diode Lights with 150pcs Resistor

These energy-efficient LEDs offer stable brightness and low power consumption. Their standard lead pitch allows for easy application on breadboards and PCB boards without additional adjustments.
Alternatively, connect the longer anode lead (+) to a positive voltage and the shorter cathode lead (-) to ground, but be sure to connect the current-limiting resistor in series with the LED to prevent damage.

Ensure correct polarity and use a current-limiting resistor; this product is not waterproof; reverse connection or operation without a resistor may damage the LED or shorten its lifespan.

# Cue lamps

5 mm **RGB common-anode** indicators (clear lens, 2.54 mm pitch). 

Wiring, pin map, and schematic: **[PINOUT.md](../PINOUT.md)**.

- Common anode (longest lead) → board **3V3** (not 5 V)
- Red / green cathodes → GPIO through a resistor
- Red: 1.8V forward voltage, Green: 2.2V Forward Voltage (tested on meter)
- Red: 75 ohms ideally, we used 67 ohms. 
- Green: 56 Ohms ideally, we used 40 ohms

- Blue: Could not test voltage drop. Possibly 3.4-4.5 V ? 
- GPIO **LOW** = that color on

# LED Pinout

- Red, Common Anode (Longest), Green, Blue
