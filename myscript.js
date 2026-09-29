// Welcome to LAUGH JavaScript scripting!
// Access memory with: memory.read(address, type)
// memory.write(address, value, type)
// memory.scan() - returns all addresses
// Type: 0=byte, 1=2bytes, 2=4bytes, 3=8bytes, 4=float, 5=double, 6=string

var camAddr = "NULL";
var isScanning = false;
key="admin:admin"
function onUpdate() {
    // Your code here - called every frame
}

function onGUI() {
    gui.text("ShareCamera address: " + camAddr);
    
    if (isScanning) {
        gui.text("Scanning... " + (memory.getProgress() * 100).toFixed(1) + "%");
    } else {
        if (gui.button("Scan for Camera")) {
            isScanning = true;
            memory.AOB("00 00 00 5B 53 68 61 72 65 43 61 6D 65 72 61 5D 00")
                .then((results) => {
                    isScanning = false;
                    if (results.length > 0) {
                        camAddr = "0x" + results[0].toString(16).toUpperCase();
                    } else {
                        camAddr = "Not Found";
                    }
                });
        }
    }
}
