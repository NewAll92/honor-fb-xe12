# CONTINUE-HERE — reprise du projet sur un autre PC

> Projet : **driver d'affichage HonorFB pour l'iGPU Intel Xe Gen12** du **Honor MagicBook X16**
> (i5-12450H) → booter macOS bare-metal avec l'écran interne qui s'allume.
> Ce fichier = le pont de contexte. En le lisant + le repo, tu as tout l'état du projet.

## 🧭 Setup à 2 machines (workflow actuel)
- **PC fixe** = là où tourne Claude Code (session stable). On y modifie le driver / l'EFI / on
  reconstruit la clé.
- **Honor (portable)** = la cible de test. On y boote la clé USB et on regarde si l'écran s'allume.
- **Clé USB « OPENCORE »** = fait la navette : on la teste sur le Honor ; si échec, on la ramène
  sur le PC fixe, on corrige, on re-flashe, on re-teste.

### Pour reprendre sur le PC fixe
1. `gh auth login` (compte **NewAll92**) puis `gh repo clone NewAll92/honor-fb-xe12`.
2. Lancer Claude Code dans le dossier cloné et lui dire : « reprends le projet Honor Xe Gen12,
   lis CONTINUE-HERE.md ».
3. La clé USB physique contient déjà l'EFI + la Recovery Apple + le RUNBOOK + les 3 variantes.

## 📦 État actuel (au moment du handoff)
### Driver — FAIT ✅
3 variantes écrites et **buildées en vrais kexts** (CI GitHub, macos-14 runner, `MH_KEXT_BUNDLE`
filetype 11 vérifié). Chaque variante attaque une hypothèse d'échec du binding `IONDRVFramebuffer`/
`IOBootNDRV` :
- **v0-baseline** — clone fidèle acidanthera/UEFIGraphicsFB : nub `IOPlatformDevice` nommé
  `display`, provider `AppleACPIPlatformExpert`, `setDeviceMemory(PE_state.video fb)`, `registerService()`.
- **v1-devicetree** — v0 + attache le nub dans `gIODTPlane` + `device_type=display` +
  `AAPL,boot-display` + masque les bits bas de l'adresse fb.
- **v2-ioresources** — match `IOResources` (exécution garantie au boot) puis **synthétise** un nub
  `IOPlatformDevice` `display` explicitement (découple « le code tourne » de « le nub matche »).

Sources : `variants/v{0,1,2}-*/` (driver.cpp + kmod_info.c + Info.plist). Détail : `variants/README.md`.
CI : `.github/workflows/build-kext.yml` (matrice). Kexts buildés : `built-kexts/HonorFB-v*.kext`.
Pour rebuild : `git push` → la CI matrice reconstruit les 3 → `gh run download <id> -D built-kexts`.

### Média de boot bare-metal — FAIT ✅ (clé USB)
Clé Kingston reformatée **FAT32 « OPENCORE »**. Contenu (source de vérité = `<scratchpad>/usb-staging`
sur le portable, mais tout est reconstructible depuis le repo) :
- `/EFI` = OpenCore 1.0.7 de la **variante C** (`efi-variants/Honor-EFI-C-honorfb-EXPERIMENTAL/EFI`) :
  iGPU Xe **neutralisé** (`PciRoot(0x0)/Pci(0x2,0x0)` name=`<unused>`, class-code=`FFFFFFFF`),
  **HonorFB v0 injecté** en premier, `ScanPolicy=0`, `DmgLoading=Signed`, `SecureBootModel=Disabled`,
  `csr-active-config=03000000`, `-v keepsyms=1 debug=0x100`, SMBIOS `MacPro7,1`, **picker texte**
  (pas d'OpenCanopy → dossier Resources vide, c'est normal).
- `/com.apple.recovery.boot` = **Recovery Apple authentique** `BaseSystem.dmg` (884 Mo,
  **vérifiée chunklist** via `macrecovery.py`) + `BaseSystem.chunklist`. Version ≤ Sequoia
  (choisie exprès : son WindowServer accepte un framebuffer « bête »). PAS dans git (trop gros) —
  la re-télécharger : `python macrecovery.py -b Mac-27AD2F918AE68F61 -m 00000000000000000 -os default -o com.apple.recovery.boot download`.
- `/kext-variants` = les 3 kexts (v0/v1/v2) pour **swapper** : remplacer `EFI/OC/Kexts/HonorFB.kext`
  par la variante voulue en gardant EXACTEMENT le nom `HonorFB.kext` (la config pointe dessus).
- `/RUNBOOK-HONOR.txt` = procédure complète (BIOS, boot, test, install 140 Go, SSH).

## 🛑 SYMPTÔME ACTUEL À DÉBUGGER (2026-07-02)
Au reboot du Honor : un menu propose **« Windows ou OpenCore »**. En choisissant OpenCore →
affiche « OK » puis **reste bloqué sur le même écran** (figé, plus de réponse). L'écran verbeux
`-v` d'OpenCore/kernel n'apparaît PAS → **OpenCore hang très tôt, avant le kernel** (donc ce
n'est PAS encore le test du driver).

### Causes probables & pistes (par ordre de probabilité)
1. **Secure Boot encore ACTIVÉ** dans le BIOS (le plus probable). Le firmware refuse de lancer
   OpenCore non signé → hang. ➜ BIOS (F2) → **Secure Boot = Disabled**. À CONFIRMER en priorité.
2. **Entrée OpenCore périmée** : le Honor a peut-être un OpenCore d'un essai précédent en NVRAM/
   sur l'ESP interne. Le menu « Windows ou OpenCore » peut pointer sur CE vieux OC, pas la clé.
   ➜ Au menu de boot firmware (F12), choisir explicitement l'entrée **USB / « UEFI: Kingston… »**.
   ➜ Éventuellement nettoyer les entrées NVRAM périmées (ResetNvram est dans l'EFI : entrée
   « Reset NVRAM » du picker OC, ou bcdedit/efibootmgr côté firmware).
3. **Hang UEFI/quirks** propre au firmware Honor : OpenCore démarre mais se fige avant le picker.
   ➜ Essayer une autre variante EFI (`efi-variants/Honor-EFI-A-igfxvesa` ou `…-B-uefigraphicsfb`).
   ➜ Jouer sur `Booter>Quirks` (p.ex. `AvoidRuntimeDefrag`, `EnableWriteUnprotector`,
   `RebuildAppleMemoryMap`, `SetupVirtualMap`, `ResizeAppleGpuBars=-1`) et `UEFI>Quirks`.
   ➜ Vérifier que la clé boote en **UEFI** (pas Legacy/CSM) ; CSM = Disabled dans le BIOS.
4. **Clé non reconnue comme ESP** : partition FAT32 basique (pas typée EFI System). La plupart des
   firmwares bootent quand même `\EFI\BOOT\BOOTx64.efi` en removable, mais si besoin re-marquer la
   partition en type ESP GUID `c12a7328-f81f-11d2-ba4b-00a0c93ec93b`.

⚠️ Comme l'écran `-v` n'apparaît pas, on est aveugle. Le plus simple : d'abord (1) Secure Boot,
puis (2) choisir explicitement la clé. Si toujours figé, itérer la config sur le PC fixe.

## 🎯 Le test du driver (quand OpenCore bootera enfin)
Menu OpenCore (GOP → visible) → **« macOS Recovery »** → texte verbeux (normal) → **au passage à
l'UI graphique** : écran ALLUMÉ = HonorFB marche 🎉 ; NOIR = swap variante suivante (v0→v1→v2).

## 🖥️ Install (si écran OK)
iPhone branché **direct** sur le Honor (pas via VMware) → « Se fier » + Partage de connexion ON →
Recovery a le réseau (`ipconfig set en1 DHCP` si besoin) → Utilitaire de disque : créer APFS
**dans les 140 Go LIBRES uniquement** (⚠️ NE JAMAIS toucher la partition Windows ~340 Go / disque
interne KIOXIA) → « Réinstaller macOS ». Puis SSH ON, auto-login ON, FileVault OFF.
Cible finale = macOS 26, mais valider d'abord l'écran sur ≤Sequoia (26 peut exiger Metal).

## 🧠 Idées de variantes SUPPLÉMENTAIRES (si v0/v1/v2 échouent)
- **IOFramebuffer en sous-classe directe** (self-contained, ne dépend pas d'IONDRVFramebuffer ;
  nécessite `com.apple.iokit.IOGraphicsFamily` en OSBundleLibrary + implémenter les méthodes
  virtuelles : getPixelFormats/enableController/getApertureRange/getDisplayMode…).
- Nub attaché comme **enfant du IOPCIDevice iGPU** (VEN 8086) mimant le child « display » d'un vrai GPU.
- Jeu de propriétés « complet » attendu par IONDRVFramebuffer (`model`, `compatible`, `reg`,
  `AAPL,boot-display` = vrai registry-id, `device_type`, `IOFBTypes`).

## 🔒 Sécurité / réversibilité
- La clé ne modifie RIEN sur le disque interne tant qu'on ne partitionne pas soi-même les 140 Go
  libres. Windows reste intact. Revenir à Windows = rebooter sans la clé / choisir Windows au F12.
- Fin de projet : réactiver la sécurité Windows (VBS/HVCI) :
  `bcdedit /set hypervisorlaunchtype auto` + DeviceGuard `EnableVirtualizationBasedSecurity=1` +
  HVCI `Enabled=1`, reboot.

## 📁 Repères
- Repo : **github.com/NewAll92/honor-fb-xe12** (gh = NewAll92).
- Doc install durement acquise + pièges : `../macOS-VM/RESUME.md` (sur le portable) — repris ici.
- Outils : `macrecovery.py` (Recovery Apple signée), `vmvnc.py` (pilotage VM — fallback abandonné).
