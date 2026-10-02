# D2RVoidDropFix - D2RLoader Plugin

A native C++20 plugin for **Diablo II: Resurrected** built using the official **D2RLoader PluginSDK**.

---

## 🎯 What Problem Does This Solve?

In vanilla Diablo II and D2R, when monsters with floating or flying animations are killed while floating over the abyss or void (such as the **Arcane Sanctuary**, **River of Flame**, or **Chaos Sanctuary**):

1. The game checks for valid ground within a small search radius via `COLLISION_GetFreeCoordinates`.
2. Because every tile in that radius is flagged with collision mask `0x801` (abyss / unwalkable void), the function returns `0` (`nullptr`).
3. The drop function aborts:
   ```c
   if (pRoom == 0) break; // Destroys item and gold drop!
   ```
4. **Valuable runes (Ber, Jah, Ohm, Vex) and unique items are permanently destroyed and lost to the void!**

---

## 🛠️ How This Plugin Fixes It

This plugin cleanly detours `COLLISION_GetFreeCoordinates` (`RVA: 0x00364E90`) through D2RLoader's tracked inline hook system:

1. **Zero Impact on Normal Gameplay:** When monsters die on valid ground, the vanilla function succeeds immediately with zero alteration.
2. **Nearest Ledge Search:** If the drop is over the void, it expands the search radius up to 64 tiles to safely drop the loot onto the nearest walkway or ledge in the Arcane Sanctuary.

---

## ⚙️ Configuration (`void-drop-fix.toml`)

```toml
[general]
# Master switch for the void drop protection hook
enabled = true

# Whether to log recovery details (default: false)
log_recoveries = false

# Search radius (in tiles) to find the nearest valid walkable ground/ledge
search_radius = 64
```

---

## ⌨️ In-Game Console Command

Open the D2RLoader console (typically `~`) and run:
```text
void-drop-fix
```
Displays whether the protection is active and the total number of items saved from the void during your session.

---

## 🏗️ Building with CMake

```powershell
mkdir build
cd build
cmake .. -A x64
cmake --build . --config Release
```

Outputs `d2rl-voiddropfix.dll` to build/Release. Copy it to your `d2rloader/plugins/` directory.

---

## 🎖️ Acknowledgements & Credits

* **[Dimentio](https://x.com/Dimentio)**: Creator of **[D2RLoader](https://d2rloader.net/)**, and the **[D2RLoader PluginSDK](https://github.com/D2RLoader/PluginSDK)**, making modern native C++ modding possible for Diablo II: Resurrected.
* **[The D2MOO Project](https://github.com/ThePhrozenKeep/D2MOO)**: For their research and reverse engineering of the Diablo II game engine, collision masks, and `COLLISION_GetFreeCoordinates` algorithms.
* **Blizzard Entertainment**: Creators of *Diablo II: Resurrected*. (This project is an unofficial open-source mod and is not affiliated with, endorsed by, or sponsored by Blizzard Entertainment.)

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
