# PS Vita Hacking Guide (for PSV Agent Remote)

> Summarized from the community's authoritative guide, **https://vita.hacks.guide** (checked 2026-10). Before each step, open the matching page of the original guide and check it — URLs and steps may change.
> Any system modification carries a risk of **bricking** your device; you do this at your own risk. Keep the **charger plugged in** throughout, and don't let the device go to sleep midway.

**Your route (firmware 3.73)**: update to 3.74 → temporary hack with HENlo → use VitaDeploy to install 3.65 + Ensō for a permanent hack → finalize setup → install VitaShell.

## 0. Preparation

- **Check your firmware version**: Settings → System → System Information.
- **Second-hand device**, or you're not sure whether it was hacked before: restore the system first with [Restoring System](https://vita.hacks.guide/restoring-system), then continue.
- **First-generation model (PCH-1000, OLED screen) without an official memory card**: first create an internal storage partition with [Creating an internal memory card](https://vita.hacks.guide/) (the page is in the guide's sidebar). **Otherwise, do not do step 2.** Second-generation models (PCH-2000) have 1 GB of built-in storage and can skip this.
- Wi‑Fi with internet access (needed to download components).
- Confirm button: this project is designed for **✕ = confirm** (Western layout).

## 1. Update from 3.73 to 3.74 (required, since your device is on 3.73)

HENlo only supports 3.65, 3.68 and 3.74. **3.73 cannot be hacked directly**; you must update to 3.74 first.

1. Settings → **System Update** → **Update Using Wi‑Fi**.
2. **Make sure the prompt says "Version 3.74"**. If it isn't 3.74, do not continue.
3. If the prompt shows the wrong version, set DNS back to automatic in your Wi‑Fi settings and try again.
4. Follow the prompts to update; the device reboots automatically when done. After the reboot, confirm it's 3.74 under Settings → System → System Information.

Original: https://vita.hacks.guide/updating-firmware-(3.74)

## 2. HENlo: temporary hack (browser exploit)

1. Open the Vita's built-in browser and go to `http://jailbreak.psp2.dev`
2. Select **Unlock my Vita** → **Unlock**, and press ✕ to confirm. If you get an error, see Troubleshooting → "A browser-based exploit is not working" in the guide, and try a few more times.
3. When the **henlo-bootstrap** screen appears, select in order:
   - **Install henkaku**
   - **Install VitaDeploy**
   - **Exit**
4. Open Settings → **HENkaku Settings** → check **Enable Unsafe Homebrew**.

Note: HENlo **stops working after a reboot**. Each time you reboot, visit the URL above again, until you've finished step 3.

Original: https://vita.hacks.guide/using-henlo

## 3. Ensō: permanent hack (downgrades from 3.74 to 3.65)

1. Open **VitaDeploy** → **Install a different OS** → **Quick 3.65 Install** (needs internet). If you don't see this option, you haven't enabled Unsafe Homebrew yet.
2. Wait for the components to download → press ✕ → read the notice, wait 20 seconds → press ✕ again.
3. The device installs 3.65 + Ensō and reboots automatically. From now on, it boots hacked every time.

Warnings:
- This step makes **permanent changes** to the system; if something goes wrong midway, it may not be recoverable. Keep it plugged in, and don't let the device sleep during the process.
- If it gets stuck on a black screen: hold the power button to turn it off, then redo this step.
- If, on reboot, it asks whether to transfer data from internal storage to the memory card, **choose No** (Yes erases the memory card).

Original: https://vita.hacks.guide/installing-enso

## 4. Finalizing setup

1. VitaDeploy → **App downloader** → select **iTLS Installer** → download → open iTLS-Enso → **Install the full iTLS package** → wait for it to reboot automatically.
2. **Turn off automatic updates**: Settings → System → Auto-Start Settings → uncheck "Download Update File for System Software".
3. (Optional, if you want to use PSN) HENkaku Settings → check Enable PSN spoofing and Enable Version Spoofing, and set Spoofed Version to `3.74`.

Original: https://vita.hacks.guide/finalizing-setup

## 4.5 Asian models: make ✕ the confirm button (optional)

Settings → **HENkaku Settings** → **○ Button Behavior**, set it to "Back / Cancel", then **reboot the Vita**.

After the reboot, the whole system UI (home screen, LiveArea, Settings, system apps) uses ✕ to confirm (tested on a real device). Without a reboot, only the Settings menu picks up the change.

Don't use Settings → Accessibility → Button Assignments: it swaps the physical buttons globally, which affects every game — Western games would flip to ○ = confirm.

This project's app is unaffected by this setting and **always uses ✕ to confirm**.

## 5. Install VitaShell (used to install this project's VPK)

Find **VitaShell** in VitaDeploy's **App downloader** and install it. If it isn't in the list, download the VPK from https://github.com/TheOfficialFloW/VitaShell/releases and install it following the matching page on vita.hacks.guide.

## 6. Install PSV Agent Remote

1. Build `psv_agent_remote.vpk` on your computer (see `vita/README` for build steps; to be filled in once the project is ready).
2. On the Vita, open VitaShell and press **SELECT** to start FTP; the screen shows `ftp://192.168.x.x:1337`.
3. On your computer, upload the VPK to `ux0:/` with an FTP client. (Finder's "Connect to Server" is read-only; use Cyberduck or FileZilla, or the command line: `curl -T psv_agent_remote.vpk ftp://<Vita's IP>:1337/ux0:/`.)
4. In VitaShell, find the VPK, press ✕ → install.
5. Once the icon appears on the home screen, open it. Before the first run, you can edit `ux0:data/psv_agent_remote/config.txt` to confirm the broker address is `192.168.2.222:1883`.

## 6.5 Hacked without a memory card; VitaDeploy and other apps vanish after inserting one

Cause: without a card, `ux0:` (where apps and data live) is on the device's internal storage. With a card inserted, `ux0:` becomes the memory card, and the system only shows apps on the card. Apps on internal storage **aren't lost** — remove the card and they reappear. The hack itself (Ensō) lives on the system partition, so it stays whether or not a card is inserted.

Fix: **with the card inserted**, reinstall the tools you need onto the card.
1. Boot with the card inserted. If it asks whether to transfer data from internal storage to the memory card, choose **No**.
2. Open the browser and go to `http://jailbreak.psp2.dev`; on the henlo-bootstrap screen, select only **Install VitaDeploy**, then Exit.
3. Open VitaDeploy → App downloader → install **VitaShell** (and iTLS too, if you need it).
4. Install this project's VPK onto the card as well (section 6).

If the browser page in step 2 won't open on an already-hacked device, boot without the card, check with the VitaShell on internal storage, and look up the matching problem on the guide's Troubleshooting page.

## 7. Network notes (specific to this project)

- The Vita's Wi‑Fi must be on the **same local network** as pop-os (`192.168.2.222`).
- First-generation models (PCH-1000) only support **2.4 GHz** Wi‑Fi; use WPA2 encryption. If your router only has 5 GHz enabled, or only WPA3, the Vita can't connect.
