## How to actually get it running

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
sudo ./Lince
```

## Some stuff to know

This is really only meant for game hacking so aslong as your doing nothing extreme it should be ok

You do need sudo to run it


## UI

Main UI
<img width="723" height="743" alt="Screenshot_20260423_123816" src="https://github.com/user-attachments/assets/43a8de0c-2330-4341-a637-45bad685afe8" />

Finished Scan + Address List
<img width="764" height="746" alt="image" src="https://github.com/user-attachments/assets/346c4eed-29a9-4ad4-83f4-d3a7688e2622" />

Settings UI

General Settings
<img width="303" height="117" alt="image" src="https://github.com/user-attachments/assets/7e504d23-ce70-4f68-92a7-259aa2fcfe07" />

Scanner Settings
<img width="205" height="179" alt="image" src="https://github.com/user-attachments/assets/5ff0f15c-5d87-4f68-83c8-9d8df2dab786" />

Visual Settings(UI Scale is a lil jank deal with it)
<img width="282" height="120" alt="image" src="https://github.com/user-attachments/assets/b36f13dc-e037-473a-9855-5ab0021e8273" />
