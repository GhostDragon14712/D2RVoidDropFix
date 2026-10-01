# D2RVoidDropFix - D2RLoader Plugin

A native C++20 plugin for **Diablo II: Resurrected** built using the official **D2RLoader PluginSDK**.

---

## 🎯 What Problem Does This Solve?

In vanilla Diablo II and D2R, when monsters with floating or flying animations (Ghosts, Wraiths, Finger Mages, Willowisps) are killed while floating over the abyss or void (such as the **Arcane Sanctuary**, **River of Flame**, or **Chaos Sanctuary**):

1. The game checks for valid ground within a small search radius (2–3 tiles) via `COLLISION_GetFreeCoordinates`.
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
2. **Nearest Ledge Search:** If the drop is over the void, it expands the search radius up to 24 tiles to safely drop the loot onto the nearest walkway or ledge in the Arcane Sanctuary.
3. **Player / Origin Fallback:** If the monster was killed deep over open space with no walkable tile anywhere nearby, the drop snaps safely to the origin/killer coordinates, ensuring no loot is ever lost.

---

## ⚙️ Configuration (`void-drop-fix.toml`)

```toml
[general]
enabled = true
log_recoveries = true
search_wider_radius_first = true
wider_search_radius = 24
fallback_to_player = true
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

Outputs `d2rl-void-drop-fix.dll`. Copy it to your `d2rloader/plugins/` directory.

---

## 🎖️ Acknowledgements & Credits

* **Dimentio**: Creator of **D2RLoader**, and the **D2RLoader Plugin SDK**, making modern native C++ modding possible for Diablo II: Resurrected.
* **The D2MOO Project**: For their monumental research and reverse engineering of the Diablo II game engine, collision masks, and `COLLISION_GetFreeCoordinates` algorithms.
* **Blizzard Entertainment**: Creators of *Diablo II: Resurrected*. (This project is an unofficial open-source mod and is not affiliated with, endorsed by, or sponsored by Blizzard Entertainment.)

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
