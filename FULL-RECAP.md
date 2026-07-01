# RÉCAP COMPLET — Session "macOS sur Honor MagicBook X16"

> Transcription structurée de toute la conversation (PC fixe AMD). À lire par la nouvelle
> session Claude sur le portable, ou par l'utilisateur. Objectif : contexte total + ne PAS
> refaire les impasses. Voir aussi `HANDOFF.md` (résumé court) et `STATUS.md` (détail driver).

---

## 0. Le matériel de l'utilisateur
- **PC fixe** : AMD Ryzen 7 5700X (8c/16t, PAS d'iGPU) + NVIDIA RTX 3080 + MSI B550-A PRO. Windows 11.
- **Portable = la CIBLE** : **Honor MagicBook X16 2024**, Intel **i5-12450H** (Alder Lake), iGPU **Intel UHD/Xe Gen12**, 16 Go LPDDR4x soudée, WiFi **Intel AX201**, USB-C 10 Gbps **sans Thunderbolt**, pas d'Ethernet.
- iPhone 17 (contexte initial). Today = 2026-07-01.

## 1. Question de départ + verdict Hackintosh
"Hackintosh possible sur mes 2 PC ?" Recherche approfondie → verdict :
- **RTX 3080 (Ampere)** : AUCUN driver macOS (Apple s'est arrêté à Pascal/High Sierra). Inutilisable.
- **iGPU Intel Xe Gen12 (Alder Lake)** : AUCUN driver macOS (dernier iGPU supporté = Ice Lake/Comet Lake 10e gen). Inutilisable.
- Ryzen 5700X : CPU OK en Hackintosh MAIS pas d'iGPU + RTX inutilisable → faudrait un GPU AMD.
- **macOS Tahoe 26 = dernier macOS x86** (26+ = Apple Silicon only). Le Hackintosh x86 se termine.
- Conclusion : les 2 PC sont des impasses pour un Hackintosh "normal".

## 2. Le vrai projet de l'utilisateur : ÉCRIRE le driver
L'utilisateur ne veut pas contourner — il veut **CRÉER le driver d'affichage** manquant pour l'iGPU Xe, injecté comme les autres kexts.
- Distinction clé : driver **Metal/accéléré** = impossible (ABI secrète Apple + ISA Xe, projet type Asahi = années). Driver **"juste l'image" (framebuffer)** = FAISABLE.
- Insight : pendant le boot verbeux le framebuffer GOP MARCHE (on voit le texte) ; l'écran devient noir quand WindowServer démarre sans `IOFramebuffer`. Le job du kext = donner ce framebuffer de boot à WindowServer.

## 3. Compiler des kexts SANS macOS (résolu)
- `ld64.lld` (LLVM sur Windows) **ne fait PAS de kext** (sort MH_EXECUTE, pas MH_KEXT_BUNDLE) → prouvé empiriquement.
- **MacKernelSDK** (acidanthera, repo public, zéro Apple ID) = headers noyau suffisants → compile OK.
- **Driver `HonorFB.kext` ÉCRIT et COMPILÉ** (approche B = nub `IOPlatformDevice` "display" ~UEFIGraphicsFB) avec LLVM portable + MacKernelSDK. `HonorFB.o` (26 Ko) + `kmod_info.o` produits, symboles IOKit corrects.
- **Link** : impossible en local (lld). → **GitHub Actions** (`.github/workflows/build-kext.yml` prêt, runner macOS = vrai ld64 Apple). Hack lld `-bundle`+patch filetype livré comme EXPÉRIMENTAL (probablement non chargeable : LC_DYLD_INFO au lieu de relocations kxld).
- Fichiers : `src/HonorFB.cpp`, `src/kmod_info.c`, `HonorFB.kext/Contents/Info.plist`, `MacKernelSDK/`, `tools/llvm/`.

## 4. EFI OpenCore pour le Honor + boot tests
- EFI OpenCore **1.0.7** construit pour i5-12450H, validé `ocvalidate` → `Honor-EFI/` (SMBIOS MacPro7,1, ProvideCurrentCpuInfo, SSDT-EC-USBX-LAPTOP + SSDT-AWAC, boot-args `-v ... -wegnoegpu`, SecureBootModel=Disabled).
- **Boot testé sur le Honor** (USB) : OpenCore OK, boot.efi charge la kernel collection, atteint `EXITBS:START` puis **écran noir + Caps Lock mort**. = mur iGPU (attendu) MAIS aussi le mur root_hash (voir §6).
- 3 variantes EFI de test dans `HonorFB/efi-variants/` : A=`igfxvesa`, B=UEFIGraphicsFB(+iGPU neutralisé), C=HonorFB. Toutes ocvalidate-clean. **Variante A testée sur Honor → même gel** (car root_hash, pas graphique).

## 5. L'USB était une image Olarila
L'utilisateur avait flashé `Olarila Sequoia 15.7.7.raw` (balenaEtcher), pas fait l'étape OpenCore Simplify. Méthode YouTube classique.

## 6. LE MUR : `OS.dmg.root_hash` (crucial, non résolu côté AMD)
Sur TOUS les essais, boot.efi gèle pareil :
```
usr/standalone/OS.dmg.root_hash → OPEN Err(0xE) NOT FOUND → RH.LRH Err → BST.FBS Err → gel EXITBS
```
- Se produit sur : Olarila (Honor + VM) **ET recovery Apple OFFICIEL** (téléchargé via macrecovery), en SecureBoot **Default ET Disabled**.
- Ce check est fait par **boot.efi lui-même**, PAS par OpenCore → `SecureBootModel` et `DmgLoading` n'y changent RIEN (prouvé, testé les deux).
- Correspond à un GitHub issue NON résolu (yusufklncc/Hackintosh-for-All-Computers #30).
- Hypothèse retenue : le **BaseSystem recovery seul** ne contient pas l'`OS.dmg` scellé que boot.efi exige ; c'est spécifique au montage **AMD + OpenCore**. Un installeur COMPLET (InstallAssistant) l'aurait.

## 7. La saga VM sur le PC fixe AMD (aboutie à un mur)
Tenté d'installer macOS en VM sur le PC AMD pour produire un disque à mettre sur le Honor :
- VMware Workstation 17.6.4 + Unlocker (appliqué OK, macOS guest boote → l'Unlocker MARCHE).
- `.vmx` fait main → bugs successifs corrigés : **PCI bridges manquants** (erreurs "No PCIe slot" pour ethernet0 puis usb_xhci → fix = ajouter `pciBridge0/4/5/6/7`), crash e1000e (dû au manque de slots), VMware qui **écrase le `.vmx`** tant qu'il tourne (fix = fermer VMware totalement avant d'éditer).
- Disque installeur Olarila attaché via **descripteur VMDK monolithicFlat** pointant sur le `.raw` (astuce : renommer en `-flat.vmdk`).
- Recovery Apple officiel téléchargé via `macrecovery.py` (Sequoia, `-b Mac-937A206F2EE63C01`), mis sur un **disque FAT32** (VHD diskpart) `com.apple.recovery.boot/BaseSystem.dmg`.
- OpenCore de TechRechard édité : `SecureBootModel Default→Disabled`, `DmgLoading Signed→Any` (via conversion vmdk→flat, extraction 7z, ré-injection sur le VHD recovery).
- **RÉSULTAT : même gel `root_hash`.** SecureBoot n'était pas la cause. Impasse AMD confirmée.

## 8. DÉCISION FINALE = Option 3 (le pivot malin)
**Installer la VM macOS sur le PORTABLE Intel (i5-12450H), pas sur le PC AMD.**
- Sur Intel, VMware installe macOS en **"vanilla"** : pas d'OpenCore, pas de patchs AMD, **pas de mur `root_hash`** (c'était le montage AMD+OpenCore qui merdait).
- Plan : VMware+Unlocker sur le portable → install macOS Sequoia (facile, on voit l'écran) → sur un **SSD USB** → activer **SSH + auto-login + FileVault OFF** → brancher le disque et **booter le Honor bare-metal** avec `Honor-EFI` + tester l'affichage via `HonorFB`/`igfxvesa` (au pire headless+SSH).
- L'utilisateur installe Claude Code sur le portable pour continuer là-bas.

## 9. Artefacts livrés (dossiers à transférer sur le portable)
- `HonorFB/` : driver (compile), toolchain LLVM, MacKernelSDK, CI, variantes EFI, STATUS.md, HANDOFF.md, ce FULL-RECAP.md.
- `Honor-EFI/` : EFI OpenCore 1.0.7 validé du Honor.
- (optionnel) `macOS-VM/com.apple.recovery.boot/BaseSystem.dmg` : recovery Sequoia officiel déjà téléchargé.

## 10. Prochaines étapes concrètes (sur le portable)
1. VMware Pro (gratuit) + Unlocker.
2. Installer macOS Sequoia en VM (Intel vanilla). Cible = un SSD USB si possible.
3. Configurer SSH / auto-login / FileVault OFF dans le macOS installé.
4. Booter ce disque bare-metal sur le Honor avec `Honor-EFI`.
5. Tenter l'affichage : `igfxvesa` d'abord, puis le driver `HonorFB` (à builder via GitHub Actions) / UEFIGraphicsFB. Sinon headless + SSH.

## Style / préférences utilisateur
Français, direct, dev/freelance à l'aise technique, veut le meilleur résultat au moindre token, fix bugs en local et push seulement quand tout est fini.
