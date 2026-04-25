# LAU
LAUGH(Linux Adaptation for Universal Game Hacking)
You can download the prebuilt from https://github.com/Silicateeeee/LAUGH/releases/latest (x86-64)


### How to actually get it running

This was made in a distrobox so if you're using that here's how to set it up

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

### 3. The stuff it needs  
You'll need a compiler and some graphics libraries for the UI:
```bash
sudo dnf install -y make gcc-c++ glfw-devel mesa-libGL-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel
```

### 4. Build it  
Go into the folder where your files are and run:
```bash
make
```

If the build fails and complains about a missing file try:
```bash
dnf provides */name-of-file.h
```

### 5. Run it  
It needs root bc it messes with other program memory:
```bash
sudo ./LAUGH
```

## Some stuff to know

This is really only meant for game hacking so aslong as your doing nothing extreme it should be ok

You do need sudo to run it


## UI

### Main UI
<img width="791" height="745" alt="image" src="https://github.com/user-attachments/assets/bcd974ec-1bca-43d2-bf2f-043f15537832" />


### Finished Scan + Address List
<img width="700" height="741" alt="image" src="https://github.com/user-attachments/assets/d69a8b90-0c0b-48ff-bb9a-d4cedd97ff1a" />


### Settings UI

General Settings
<img width="303" height="117" alt="image" src="https://github.com/user-attachments/assets/7e504d23-ce70-4f68-92a7-259aa2fcfe07" />

Scanner Settings
<img width="205" height="179" alt="image" src="https://github.com/user-attachments/assets/5ff0f15c-5d87-4f68-83c8-9d8df2dab786" />

Visual Settings(UI Scale is a lil jank deal with it)
<img width="282" height="120" alt="image" src="https://github.com/user-attachments/assets/b36f13dc-e037-473a-9855-5ab0021e8273" />
