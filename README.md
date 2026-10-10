# Heretic II Remastered

![Logo](Logo.png)
A Very Special Thank You to the efforts of the developers of Heretic2R (Source Port), and spacefarergames without this none of this would be possible! https://github.com/m-x-d/Heretic2R ; https://github.com/spacefarergames/Heretic2Remastered

Heretic II Remastered is a reverse-engineered source port of Heretic II (1998, Raven Software), with HD enhancements and modern engine improvements. Now completely 64bit with modern game control support out of the box and automatic detection of Original game data (Steam / GOG / CD) The best way to play Heretic II now and for the forseeable future!
> This is a Free, Non-profit passion project.


<img width="3799" height="2131" alt="Heretic" src="https://github.com/user-attachments/assets/34d15980-28d1-4c75-b8c1-db56da9c99d4" />




## Changes from R8.0

* Multi Pass reflections water planes
It uses same effect as 3dmark01 lobby scene for water.
(There are still issues where not everything is being loaded.)

* Bump mapping
Currently disabled, lack of proper textures, and water texture to do decent visuals.
(Can be played with r_bump_scale_world 0.3 and higher, and r_bump_scale_water 0.5 and higher.)

* Rebuild in mingw-w64 cross compiler
Now it can be compiled with cmake, no longer needs msvc.
It means full linux support.
 
* Fixed in-game crashes, and crash on loading
Self explanatory - game doesn't crash anymore.

* Bodies no longer disappear
Corpses stay by default, can be changed with g_keep_corpses 0, to enable cleanup after 10-20sec.

* Particles now cast light
Particles now lights, this includes fires, particles from weapons etc.
There are some bugs, and missing lights from all weapons (or too small lights WIP)

* Different color profiles (Adobe RGB, rec2020, DCI-P3, SRGB)

## Installation

**Game data:**  
Heretic II Remastered requires Heretic II game data in order to run.

**Automatic Installation**
Since R7 release, Heretic 2 Remastered can automatically find and copy the required PAK files from any copy installed on your PC through Steam, GOG or original CD installation.
If it finds the required data, it will copy in the background and start as soon as it's ready. No setup needed.
In the event that it cannot find your installed copy, proceed to manual installation.

**Manual Installation:**  
 
– Copy "`Htic2-0.pak` and `Htic2-1.pak`) into the `base` folder of Heretic II Remastered.
- Make sure you've downloaded the latest version of Remastered, which includes the base.pak, containing all the necessary HD textures, music OGGs, and HD videos. If you have an older version without base.pak, you can either update to the latest version or extract `base.pak` from the latest release and place it in the `base` folder.
- If you have the original game CD, the engine will **automatically detect it** and extract the required PAK files — see the **CD Auto-Detection** section below. You can also copy the files manually.
- If you don't have the original game CD, you can purchase Heretic II from GOG or Steam, which both include the necessary game data files. Just make sure to point Remastered to the correct `base` folder where those files are located.

**HD textures:**  
Place PNG replacement textures in "**base\HDTextures**", mirroring the original texture paths. The renderer will automatically use them in place of the original `.m8`/`.m32` textures.  
HD textures can also be loaded from `base.pak`.

**HD videos:**  
Place MP4 or MKV cinematics in "**base\video**". The game will play them in place of the original `.cin`/`.smk` files.  
HD videos can also be loaded from `base.pak`.

## base.pak and the PAK2 format

All Remastered game data — including HD textures, music OGGs, and HD videos — can be packed into a single `base.pak` archive using the included **MakePak** tool. When present in the `base` directory, `base.pak` is automatically loaded by the engine alongside the original `Htic2-0.pak` / `Htic2-1.pak` files.

The original Quake/Heretic II PAK format limits filenames to 56 characters, which is too short for many HD texture paths. To solve this, a new **PAK2** format is used:

| Feature | PAK (original) | PAK2 (extended) |
|---|---|---|
| Header ident | `PACK` | `PAK2` |
| Filename length | 56 characters | 128 characters |
| Directory entry struct | `dpackfile_t` | `dpackfile2_t` |

The engine auto-detects both formats when loading any `.pak` file, so original `Htic2-*.pak` files continue to work unchanged.

### MakePak tool

The `MakePak` tool packs a directory tree into a PAK2 archive:

```
MakePak.exe <inputdir> [outputfile]
```

- `inputdir` — path to the directory to pack (e.g. the `base` game data folder).
- `outputfile` — output `.pak` filename (defaults to `base.pak` in the current directory).

The following file types are excluded from packing: `.cfg`, `.dll`, `.pak`.

### How PAK loading works

The engine loads content from `base.pak`, `Htic2-0.pak` through `Htic2-9.pak`, and loose files. The subsystems that previously used direct filesystem I/O have been updated to go through the engine's `FS_LoadFile` / `FS_FOpenFile` functions first, falling back to direct file access for backward compatibility:

- **HD textures** — the GL3 renderer tries `FS_LoadFile` (which searches PAK files and loose directories) before falling back to direct `fopen`.
- **Music (OGG)** — the sound backend tries `FS_LoadFile` with `stb_vorbis_open_memory`, falling back to `stb_vorbis_open_filename` for loose files.
- **HD videos (MP4/MKV)** — the client tries loose files first, then extracts from PAK to a temporary file for Windows Media Foundation playback (which requires a file path).

## Technical notes

* Savegames/configs/screenshots/logs are stored in "**%USERPROFILE%\Saved Games\Heretic2R**".
* This version of the game does not include Multiplayer, and is not a planned feature.
* Savegames are **NOT** compatible with original H2 savegames nor prior 32bit builds.
* Framerates above 60 FPS are supported.

## Planned features

* Vulkan rewrite + test technology
* Test First-Person-Perspective
* Re-do skyboxes
* Re-create 3d models to make them look more up to date.
* Create PBR workflow textures.
* SSAO for objects (Not for water)
* Gentle GI (potentially pre-baking light planes.)
* Create nicer particle effects with physical collisions
* Add water displacement when play or mobs move in it.
* Add water refraction things underwater.
* Get water caustics to work.
* Re-add staff effects that are currently ?gone? or aren't working.
* HDR?
* 3D Sound

Got any ideas? Create new issue, sell it to me.
