# LAUGH JavaScript API Documentation

LAUGH provides a JavaScript scripting engine (QuickJS) for extending functionality. Scripts can access memory operations and build custom GUI panels.

## Memory API

### memory.read(address, type)
Read memory at a specific address.

**Parameters:**
- `address` (number): Memory address (hex like `0x1000` or decimal like `4096`)
- `type` (number): Data type:
  - `0` = byte (uint8)
  - `1` = 2 bytes (uint16)
  - `2` = 4 bytes (uint32)
  - `3` = 8 bytes (uint64)
  - `4` = float
  - `5` = double
  - `6` = string (up to 64 chars)

**Returns:** The value read from memory

```javascript
let val = memory.read(0x10000, 2);  // read 4 bytes as integer
let str = memory.read(0x5000, 6);  // read string
let flt = memory.read(0x2000, 4);  // read float
```

### memory.write(address, value, type)
Write a value to memory.

**Parameters:**
- `address` (number): Memory address to write to
- `value` (number): Value to write
- `type` (number): Data type (same as read)

**Returns:** `true` on success

```javascript
memory.write(0x10000, 42, 2);    // write 42 as 4-byte integer
memory.write(0x10000, 3.14, 4); // write float
memory.write(0x5000, "hello", 6); // write string
```

### memory.scan()
Get all addresses from the current scan results.

**Returns:** Array of address strings

```javascript
let addrs = memory.scan();
for (let i = 0; i < addrs.length; i++) {
    log("Found: " + addrs[i]);
}
```

### memory.getProcessInfo()
Get information about the currently attached process.

**Returns:** Object `{ name: "processname", pid: 1234 }`

```javascript
let proc = memory.getProcessInfo();
log("Attached to: " + proc.name + " (PID: " + proc.pid + ")");
```

---

## GUI API

### Windows

#### gui.beginWindow(name)
Begin a new window. Must be paired with `gui.endWindow()`.

**Parameters:**
- `name` (string): Window title

**Returns:** `true` if window is open and being rendered

```javascript
if (gui.beginWindow("My Window")) {
    gui.text("Hello inside window!");
    gui.endWindow();
}
```

#### gui.endWindow()
End the current window. Must follow `gui.beginWindow()`.

---

### Basic Controls

#### gui.button(label)
Create a clickable button.

**Parameters:**
- `label` (string): Button text

**Returns:** `true` when button is clicked (one frame only)

```javascript
if (gui.button("Click Me")) {
    log("Button was clicked!");
}
```

#### gui.text(text)
Display text in the GUI.

**Parameters:**
- `text` (string): Text to display

```javascript
gui.text("Hello World!");
gui.text("Value: " + myVariable);
```

#### gui.checkbox(label, value)
Create a checkbox.

**Parameters:**
- `label` (string): Checkbox label
- `value` (boolean): Initial state

**Returns:** Current checkbox state

```javascript
let isChecked = gui.checkbox("Enable Feature", false);
if (isChecked) {
    log("Feature enabled!");
}
```

#### gui.inputInt(label, value)
Integer input field.

**Parameters:**
- `label` (string): Field label
- `value` (number): Initial value

**Returns:** Current value

```javascript
let num = gui.inputInt("Enter number", 0);
```

#### gui.inputFloat(label, value)
Float input field.

**Parameters:**
- `label` (string): Field label
- `value` (number): Initial value

**Returns:** Current value

```javascript
let fnum = gui.inputFloat("Enter float", 0.0);
```

#### gui.sliderFloat(label, value, min, max)
Float slider control.

**Parameters:**
- `label` (string): Slider label
- `value` (number): Initial value
- `min` (number): Minimum value
- `max` (number): Maximum value

**Returns:** Current value

```javascript
let volume = gui.sliderFloat("Volume", 0.5, 0.0, 1.0);
```

---

### Layout

#### gui.sameLine()
Place next control on same line as previous.

```javascript
gui.text("Name:");
gui.sameLine();
gui.inputInt("Age:", 0);
```

#### gui.separator()
Draw a horizontal separator line.

```javascript
gui.text("Section 1");
gui.separator();
gui.text("Section 2");
```

#### gui.beginChild(id, width, height)
Begin a child region/scroll area.

**Parameters:**
- `id` (string): Child region ID
- `width` (number, optional): Width
- `height` (number, optional): Height

**Returns:** `true` if child is visible

```javascript
if (gui.beginChild("scroll", 200, 300)) {
    for (let i = 0; i < 20; i++) {
        gui.text("Item " + i);
    }
    gui.endChild();
}
```

#### gui.endChild()
End a child region. Must follow `gui.beginChild()`.

---

### Trees & Lists

#### gui.treeNode(label)
Begin a tree node (collapsible).

**Parameters:**
- `label` (string): Node label

**Returns:** `true` if node is open/expanded

```javascript
if (gui.treeNode("Options")) {
    gui.checkbox("Option A", false);
    gui.checkbox("Option B", false);
    gui.treePop();
}
```

#### gui.treePop()
End a tree node. Must follow `gui.treeNode()`.

#### gui.combo(label, current, items)
Combo box (dropdown).

**Parameters:**
- `label` (string): Combo label
- `current` (number): Current selection index
- `items` (array): Array of string items

**Returns:** Selected index

```javascript
let idx = gui.combo("Color", 0, ["Red", "Green", "Blue"]);
```

#### gui.progressBar(fraction)
Display a progress bar.

**Parameters:**
- `fraction` (number): Progress 0.0 to 1.0

```javascript
gui.progressBar(0.75);  // 75% complete
```

---

### Screen Information

#### gui.getScreenSize()
Get screen dimensions.

**Returns:** Object `{ x: width, y: height }`

```javascript
let size = gui.getScreenSize();
log("Screen: " + size.x + "x" + size.y);
```

#### gui.getFrameCount()
Get current frame number.

**Returns:** Frame count (number)

```javascript
let frame = gui.getFrameCount();
```

#### gui.getDeltaTime()
Get time since last frame (in seconds).

**Returns:** Delta time (number)

```javascript
let dt = gui.getDeltaTime();
let speed = 100 * dt;  // 100 units per second
```

#### gui.getMousePos()
Get current mouse position.

**Returns:** Object `{ x: x, y: y }`

```javascript
let pos = gui.getMousePos();
```

---

### Input State

#### gui.isKeyPressed(keyCode)
Check if a key was pressed.

**Parameters:**
- `keyCode` (number): Key code (ImGui key constants)
- `repeat` (boolean, optional): Allow key repeat

**Returns:** `true` if key was pressed

```javascript
if (gui.isKeyPressed(32)) {  // Space key
    log("Space pressed!");
}
```

#### gui.isMouseClicked(button)
Check if mouse button was clicked.

**Parameters:**
- `button` (number): 0 = left, 1 = right, 2 = middle

**Returns:** `true` if clicked

```javascript
if (gui.isMouseClicked(0)) {
    log("Left click!");
}
```

---

### Custom Drawing

These functions draw directly on the window.

#### gui.drawLine(x1, y1, x2, y2, color, thickness)
Draw a line.

```javascript
gui.drawLine(10, 10, 100, 10);
```

#### gui.drawRect(x1, y1, x2, y2, color, thickness)
Draw a rectangle.

```javascript
gui.drawRect(10, 10, 100, 50);
```

#### gui.drawCircle(x, y, radius, color, segments, thickness)
Draw a circle.

```javascript
gui.drawCircle(50, 50, 25);
```

#### gui.drawText(text, x, y, color)
Draw text at position.

```javascript
gui.drawText("Hello", 10, 10);
```

---

### Global Functions

#### log(message, ...)
Print message to console.

```javascript
log("Value:", 42);
log("Name:", "John", "Age:", 25);
```

---

## Example Scripts

### Simple Counter
```javascript
var count = 0;

function onGUI() {
    gui.beginWindow("Counter", function() {
        gui.text("Count: " + count);
        
        if (gui.button("Increment")) {
            count = count + 1;
            log("Count is now: " + count);
        }
        
        gui.sameLine();
        
        if (gui.button("Reset")) {
            count = 0;
            log("Counter reset!");
        }
    });
}
```

### Health Bar Display
```javascript
var healthAddr = 0x00123456;  // Set your address

function onGUI() {
    if (gui.beginWindow("Player Stats")) {
        let health = memory.read(healthAddr, 2);
        
        gui.text("Health: " + health + "/100");
        gui.progressBar(health / 100);
        
        gui.separator();
        
        if (health < 25) {
            gui.text("WARNING: LOW HEALTH!");
        }
        
        gui.endWindow();
    }
}
```

### Custom Slider Writer
```javascript
var targetAddr = 0x00400000;

function onGUI() {
    if (gui.beginWindow("Value Writer")) {
        let val = gui.sliderFloat("Value", 0, 0, 100, "%.1f");
        
        gui.text("Writing: " + val);
        
        if (gui.button("Write Now")) {
            memory.write(targetAddr, val, 4);
            log("Wrote " + val + " to " + targetAddr);
        }
        
        gui.endWindow();
    }
}
```

---

## Value Types Reference

| Type ID | Name     | Size    |
|--------|---------|---------|
| 0      | byte    | 1 byte  |
| 1      | 2bytes  | 2 bytes |
| 2      | 4bytes  | 4 bytes |
| 3      | 8bytes  | 8 bytes |
| 4      | float   | 4 bytes |
| 5      | double  | 8 bytes |
| 6      | string  | varies  |