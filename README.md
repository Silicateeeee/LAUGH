# Linux Cheat Engine

So, I made this simple cheat engine for Linux. It's basically a memory scanner you find a value in a game or program, and you can change it. It's got the classic layout you'd expect: scanner on the right, results on the left, and a list at the bottom to keep track of stuff.

## How to actually get it running

I wrote this inside a Distrobox (Fedora-based), so if you're using that, here's how to get it working.

### 1. Create your box
If you haven't created your box yet:
```bash
distrobox create Name-Of-Choice
```
### 2. open your box
If you haven't opened your box yet:
```bash
distrobox enter Name-Of-Choice
```

### 3. Grab the stuff it needs
You'll need the compiler and the graphics libraries to get the UI working. Just run:
```bash
sudo dnf install -y make gcc-c++ glfw-devel mesa-libGL-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel
```

### 4. Build it
Just go into the folder where you put all the files and run:
```bash
make
```

### 5. Run it
Because this thing needs to reach into other programs' memory to change values, you have to run it with root permissions:
```bash
sudo ./linuxce
```

## Some stuff to know
It's not as polished as the big commercial tools, but it does the job for simple stuff.
Sudo IS needed so once built run `sudo ./linuxce`.

If the build fails and yells at you about a missing file, just try `dnf provides */name-of-file.h` to see what package you're missing.
