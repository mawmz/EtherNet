<img width="640" height="360" alt="maxresdefault" src="https://github.com/user-attachments/assets/c31dad90-f2f6-464d-8dfe-4ece381f30d3" />

*bahahahahahha!*

# EtherNet
### Xenoblade Chronicles 2 Multiplayer

Bring a friend to Alrest.

**EtherNet** is an experimental local multiplayer mod for *Xenoblade Chronicles 2*. Explore the world and play through the entire game with a friend!
It gives full control of the second Driver in the party to a second controller, allowing complete co-op in the open world and in battle.

This is **local, shared-screen co-op**. Despite the name, EtherNet does not yet contain native online play, though that is a planned feature. For now, emulation + Parsec is the only way to play online

## Setup Requirements

You'll need...
- A Nintendo Switch set up to run Atmosphere
- *Xenoblade Chronicles 2* version **v1.0.0-v2.0.0** (or just the `main` exefs for that version with installed update **v2.1.0**. ***v3.0.0+*** *is untested.*)
  - Despite 'supporting' all of these versions in theory, the version this mod was developed on is **v1.5.1**, and that'll be the only version with full compatibility.
- For online play, you'll need a PC capable of emulating Xenoblade 2. For more information about this, go to the `Yuzu + Parsec` section below

## Installation and Hosting (Nintendo Switch)
For Nintendo Switch users intending for simple couch play, hosting is simple.
- Install your **v1.0.0-v2.0.0**/**v1.5.1** update, or place your version's 'main' exefs at `sd:\atmosphere\contents\0100E95004038000\exefs\` and install update **v2.1.0**
- Boot the game and connect another controller, and away you go!
- If something buggy occurs while playing, Press `L + R + ZL + ZR` to open a debug menu.
- Have fun!

## Installation and Hosting (Yuzu + Parsec)
**An archived build of Yuzu is recommended for emulation. Eden has issues with Xenoblade 2's shaders. Ryujinx and its deviations may work fine in that regard, but tend to be a bit unstable with this game in my experience.**

If you intend for couch play on an emulator, simply follow the instructions for Switch above, but place your `main` exefs at `sd:\atmosphere\contents\0100E95004039001\exefs\` instead.

### For hosting via Parsec
- Download Yuzu emulator and follow the setup process
- Install Xenoblade Chronicles 2
- Install your **v1.0.0-v2.0.0**/**v1.5.1** update, or place your version's `main` exefs at `sd:\atmosphere\contents\0100E95004039001\exefs\` and install update **v2.1.0**
- Install any DLC you'd like to play with.
- Open the game once to confirm the mod is properly installed. If the mod is installed correctly, you should see log messages in the top left. If not, ensure the mod is installed at the proper location. Where you install this mod on emulator differs from the Nintendo Switch install location.
- Install Parsec on your PC
- Once Parsec is installed, open it. Go to `Settings > Host` and scroll down until you see `Parsec Virtual USB Gamepads (Beta)` and install the driver. Restart your PC.
- After restarting, go back to `Settings > Host` and enable the `Parsec Virtual USB Gamepads` setting and set the `Gamepad` type to `Xbox 360`.
- Configure any other hosting settings to your preferences. If your internet is good, feel free to set your settings a bit higher. Make sure you account for where the person joining you is located in the world.
- Go back to `Computers` and hit `Share`, then send the link to your player 2 to have them join the session. If setup went correctly for the remote player, a 'Windows connect' noise should be heard upon joining, indicating their controller has connected successfully. Now is a good time to connect your controller too!
- In Yuzu, go to `Emulation > Configure > Controls` and select your and player 2's controllers under `Input Device`. Both should appear if the Virtual USB Gamepad is installed.
- Test in-game that both controllers work correctly. If they do, congratulations! Have fun exploring and battling throughout Alrest!

### For joining via Parsec
This process is much simpler.

- Install Parsec on your PC
- Connect a controller to your PC. Don't have an Xbox/PS Controller? Thats fine! Install [Switch2Connect](https://github.com/TommyWabg/Switch2Connect/releases/latest/) to connect Switch 2 controllers. Playing with a controller is highly recommended.
- After opening, it may prompt you to install a driver. Click **Yes**.
- After the main application launches, hit the Sync button on your Joy-Con 2/Pro Controller 2. It should automatically pair.
- Go to `Settings > Gamepad` and select your controller in the dropdown menu. Test the buttons on your controller to be sure they're working correctly.
- Click the Parsec link that the host shared with you, and join the session.
- That should be all for this side; everything else is up to the host. Enjoy Alrest!

### Questions? Bugs? Issues?
Contact me on Discord at `mawmz` or open a Github issue for this repository.

## Legal Notice
EtherNet is licensed under the [MIT License](LICENSE). *Xenoblade Chronicles 2*
and related names and assets belong to Nintendo Co., Ltd. and Monolith Software Inc. This is an
unofficial fan project and is not affiliated with or endorsed by either company.
If you represent a rights holder and have concerns about material contained in this
repository, please contact `ethernet.legal@gmail.com` so the matter can be reviewed promptly.
