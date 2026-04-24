
// Welcome to LAUGH JavaScript scripting!
// Access memory with: memory.read(address, type)
// memory.write(address, value, type)
// memory.scan() - returns all addresses
// Type: 0=byte, 1=2bytes, 2=4bytes, 3=8bytes, 4=float, 5=double, 6=string

function onUpdate() {
    // Your code here - called every frame
wasd
}

function onGUI() {
    // Your custom GUI code here
    gui.text("Script Panel loaded!");
    if (gui.button("Click Me")) {
        log("Button clicked!");
    }
}
