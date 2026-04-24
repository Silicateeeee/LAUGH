/**
 * NEVERMENU ported to LAUGH JavaScript Engine
 */

var camAddr = "Not Found";
var isScanning = false;

// Helper to scan and replace bytes at all found addresses
async function AOBRep(search, change) {
    log("Scanning for patch: " + search);
    try {
        const results = await memory.AOB(search);
        if (results.length > 0) {
            for (let addr of results) {
                // Using type 7 (AOB Pattern) for patching
                memory.write(addr, change, 7);
            }
            log("Successfully patched " + results.length + " locations.");
        } else {
            log("Patch failed: Pattern not found.");
        }
    } catch (e) {
        log("Error during AOBRep: " + e);
    }
}

// Little Endian Hex Helper
function to_le_hex(n) {
    let bytes = [];
    for (let i = 0; i < 4; i++) {
        bytes.push(((n >> (i * 8)) & 0xFF).toString(16).padStart(2, '0').toUpperCase());
    }
    return bytes.join(" ");
}

// Gravity Functions
const GRAV_AOB = 'C3 F5 1C C1 00 00 00 00 0A D7';
function grvUp()      { AOBRep(GRAV_AOB, '00 40 1C 46 00 00 00 00 0A D7'); }
function grvDown()    { AOBRep(GRAV_AOB, '00 40 1C C6 00 00 00 00 0A D7'); }
function grvLow()     { AOBRep(GRAV_AOB, '00 00 00 C0 00 00 00 00 0A D7'); }
function grvRestore() {
    const original = 'C3 F5 1C C1 00 00 00 00 0A D7';
    const patterns = [
        '00 00 00 C0 00 00 00 00 0A D7',
        '00 40 1C 46 00 00 00 00 0A D7',
        '00 00 4C C0 00 00 00 00 0A D7',
        '00 00 C8 C2 00 00 00 00 0A D7'
    ];
    patterns.forEach(p => AOBRep(p, original));
}

// Item/Weapon Functions
function rapidFire()       { AOBRep('33 33 D3 3F 00 00 00 00 00 00', '00 3C 1C 46 00 00 00 00 00 00'); }
function cam_to_shotgun()  { AOBRep('00 00 00 5B 53 68 61 72 65 43 61 6D 65 72 61 5D 00', '00 00 00 5B 41 72 65 6E 61 5F 53 68 6F 74 67 75 6E 5D 00'); }
function clip_to_railgun() { AOBRep('00 00 00 5B 46 65 65 64 62 61 63 6B 54 6F 6F 6C 5D 00', '00 00 00 5B 41 72 65 6E 61 5F 52 61 69 6C 47 75 6E 5D 00'); }

function set_ammo(ammo_count) {
    const search = '4A 03 00 00 00 06 00 00 00';
    if (ammo_count !== undefined) {
        AOBRep(search, "4A " + to_le_hex(ammo_count) + " 06 00 00 00");
    } else {
        // Infinite ammo default
        AOBRep(search, '4A FF E0 F5 05 06 00 00 00');
    }
}

function onUpdate() {
}

function onGUI() {
        gui.text("Gravity Controls");
        if (gui.button("Grav Up")) grvUp();
        gui.sameLine();
        if (gui.button("Grav Down")) grvDown();
        
        if (gui.button("Moon Grav")) grvLow();
        gui.sameLine();
        if (gui.button("Restore Grav")) grvRestore();

        gui.separator();
        gui.text("Inventory Swaps");
        if (gui.button("Replace Camera (Shotgun)")) cam_to_shotgun();
        if (gui.button("Replace Clip (Railgun)")) clip_to_railgun();

        gui.separator();
        gui.text("Combat Hacks");
        if (gui.button("Infinite Ammo")) set_ammo();
        gui.sameLine();
        if (gui.button("Rapid Fire")) rapidFire();

        if (memory.isScanning()) {
            gui.separator();
            gui.text("Scan Progress: " + (memory.getProgress() * 100).toFixed(1) + "%");
            gui.progressBar(memory.getProgress());
        }
}
