# HANDOFF — Projet macOS sur Honor MagicBook X16 (à lire par la session Claude du PORTABLE)

> Colle ce fichier (ou son résumé) au démarrage de Claude Code sur le portable Honor.
> Il résume ~une journée de travail faite sur le PC fixe AMD. On PIVOTE sur le portable Intel.

## Le matériel
- **Cible = Honor MagicBook X16 2024** : Intel **i5-12450H** (Alder Lake), iGPU **Intel UHD/Xe Gen12**, 16 Go LPDDR4x, WiFi Intel AX201, **PAS de Thunderbolt**. C'est CE portable.
- Autre machine : PC fixe Ryzen 7 5700X + RTX 3080 (là où le travail précédent a été fait).

## Le but
Faire tourner macOS **bare-metal sur le Honor**. Obstacle unique et non contournable : **l'iGPU Xe n'a AUCUN driver macOS** → écran noir au hand-off graphique. Le CPU, lui, boote macOS très bien.

## Ce qui est DÉJÀ fait et prouvé (sur le PC fixe)
1. **EFI OpenCore 1.0.7 pour le Honor** validé `ocvalidate` → `C:\Users\samym\Downloads\Honor-EFI\` (à copier sur le portable). Boot testé sur le Honor : OpenCore OK, `boot.efi` atteint EXITBS puis écran noir (= mur iGPU attendu).
2. **Driver `HonorFB.kext`** écrit et **il COMPILE** (approche B = nub `IOPlatformDevice` "display" ~UEFIGraphicsFB, expose le framebuffer de boot à WindowServer). Toolchain : LLVM portable + `acidanthera/MacKernelSDK`, **zéro macOS/Apple ID**. Dossier `C:\Users\samym\Downloads\HonorFB\`. **Link impossible avec lld** (fait du MH_EXECUTE, pas de kext) → **vraie voie = GitHub Actions** (`.github/workflows/build-kext.yml` prêt, runner macOS = vrai ld64). Voir `STATUS.md`.
3. 3 variantes EFI de test dans `HonorFB/efi-variants/` (A=igfxvesa, B=UEFIGraphicsFB, C=HonorFB).

## Le mur qui a fait pivoter (IMPORTANT)
Impossible d'INSTALLER macOS pour obtenir un disque à mettre sur le Honor :
- On ne peut pas piloter l'installeur SUR le Honor (écran noir).
- Sur le **PC fixe AMD**, l'install en VM a buté sur `usr/standalone/OS.dmg.root_hash NOT FOUND → BST.FBS Err → gel EXITBS`, sur **toutes** les images (Olarila ET recovery Apple officiel), en SecureBoot Default ET Disabled. Ce check est fait par boot.efi, non contourné par SecureBootModel/DmgLoading. C'est un problème **spécifique au montage AMD + OpenCore**.

## LE PLAN sur le portable Intel (option 3 — le chemin malin)
Sur Intel, une install macOS en VMware est **"vanilla" et triviale** : pas d'OpenCore, pas de patchs AMD, **pas de mur root_hash** (c'est le cas AMD qui merdait).
1. **VMware Workstation Pro (gratuit) + Unlocker** sur le portable.
2. **Installer macOS Sequoia dans une VM** (Intel host = simple ; utiliser un vrai installeur/recovery, la VM voit l'écran).
3. Installer sur un **SSD USB** (ou disque virtuel → convertir). C'est le "disque système macOS".
4. Dans macOS installé : activer **SSH (Remote Login)** + **auto-login** + **FileVault OFF**.
5. Brancher ce disque et **booter le Honor bare-metal** avec `Honor-EFI` (+ tester le driver `HonorFB` / `igfxvesa` pour l'affichage).
   - Écran local sur le Honor = mur iGPU → au mieux **headless + SSH**, ou l'expérience framebuffer (HonorFB / UEFIGraphicsFB) pour tenter une image.

## À copier du PC fixe vers le portable (clé USB)
- `C:\Users\samym\Downloads\HonorFB\` (driver, toolchain, EFI variants, STATUS, ce HANDOFF)
- `C:\Users\samym\Downloads\Honor-EFI\` (l'EFI validé du Honor)
- (optionnel) `com.apple.recovery.boot\BaseSystem.dmg` (recovery Sequoia officiel déjà téléchargé, dans `macOS-VM\`)

## Contraintes utilisateur
- Français. Va droit au but. Le user est dev/freelance, à l'aise technique, veut LE meilleur résultat au moindre token. Fix bugs en local, push seulement quand tout est fini.
