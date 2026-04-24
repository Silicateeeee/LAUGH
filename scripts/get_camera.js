var camAddr = "Not Found";
var isScanning = false;

function onUpdate() {
    // Background logic here
}

function onGUI() {
    gui.text("ShareCamera address: " + camAddr);

    if (isScanning) {
        gui.text("Scanning... " + (memory.getProgress() * 100).toFixed(1) + "%");
    } else {
        if (gui.button("Scan for Camera")) {
            isScanning = true;
            log("Starting AOB scan...");

            memory.AOB("00 00 00 5B 53 68 61 72 65 43 61 6D 65 72 61 5D 00")
                .then((results) => {
                    isScanning = false;
                    if (results.length > 0) {
                        // Convert BigInt address to a hex string for display
                        camAddr = "0x" + results[0].toString(16).toUpperCase();
                        log("Found camera at: " + camAddr);
                        memory.write(camAddr, "00 00 00 5B 41 72 65 6E 61 5F 53 68 6F 74 67 75 6E 5D 00", 0);
                        log("Overwrote ShareCamera to Arena_Shotgun!");
                    } else {
                        camAddr = "Not Found";
                        log("Scan finished with no results.");
                    }
                });
        }
    }
}
