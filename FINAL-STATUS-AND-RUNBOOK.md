# HonorFB — Statut final & Runbook

Honor MagicBook X16 (i5-12450H, **iGPU Intel Xe Gen12 = zéro driver macOS**). Objectif : faire tourner macOS bare‑metal avec l'écran interne piloté par un driver maison.

---

## 1. CE QUI EST ACCOMPLI (prouvé)

- ✅ **macOS boote bare‑metal sur le Honor.** Le mur `ExitBootServices` (gel à `LOG:EXITBS:START`) est vaincu.
  - **LE fix** : `Kernel > Quirks > ProvideCurrentCpuInfo = FALSE` **+** spoof CPUID Comet Lake
    (`Kernel > Emulate > Cpuid1Data = 55 06 0A 00 …(16o)`, `Cpuid1Mask = FF FF FF FF 00 …`).
  - Red herrings écartés (NE PAS y revenir) : tous les combos memory‑map (Rebuild/Sync/WriteUnprotector/
    SetupVirtualMap/DevirtualiseMmio, y compris le combo ThinkBook14), `csr 0x800`, version du recovery
    (Monterey == Sequoia), CpuTopologyRebuild, `cpus=1`, ForceExitBootServices.
  - Débloqué en passant **OpenCore en build DEBUG** (log ESP `opencore-*.txt` ; `OCABC: MAT support is 1`).
- ✅ **On contrôle l'écran interne Xe.** La sonde `vpaint` peint des bandes de couleur sur le panneau interne.
  - Framebuffer confirmé : `PE_state.video` base = **0x4000000000**, **1920×1200**, 32 bpp, rowBytes 7680.
- ✅ **Driver `IOFramebuffer` maison (v3) chargé.** En **injectant `IOGraphicsFamily.kext`** (extrait du recovery,
  bundle plat, en 1er dans `Kernel > Add`), `HonorFB_v3` se linke, `IOFramebuffer::start` réussit, EDID publié.
- ✅ **LOGO APPLE + BARRE DE PROGRESSION s'affichent sur l'écran interne** (Monterey ET Big Sur). Le boot
  graphique macOS dessine via notre driver. **Personne n'avait fait ça sur un Xe Gen12.**

## 2. LE MUR (confirmé de tous les angles) : le BUREAU

- ❌ WindowServer **moderne (Big Sur → Sequoia) exige un GPU Metal** pour composer le bureau → gel à ~50 %
  (au handoff `CoreAnalyticsHub` / `GTrace 1`). Confirmé sur **Monterey** ET **Big Sur** (recovery).
- ❌ **High Sierra** (dernier macOS qui compose en software) : gèle à l'**ACPI module‑level opcode** — son
  ACPICA 2016 est trop vieux pour l'ACPI Alder Lake. Cul‑de‑sac.
- ❌ **`-igfxvesa`** (piste « software WindowServer » de Big Sur/Catalina) exige un iGPU Intel *reconnu* par
  WhateverGreen. Le Xe Gen12 n'est reconnu par **aucun** driver Apple → `-igfxvesa` sans cible, sans effet.
- ❌ **Spoof device‑id iGPU** : Xe = aucune cible de spoof (Apple s'arrête à Gen9). Mort.
- ⚠️ **OCLP non‑Metal** = le SEUL mécanisme qui casserait le mur Metal (il patche WindowServer pour composer
  en CPU). Mais il est conçu pour de vieux GPU à driver *legacy* ; sur notre **framebuffer nu** = pari non prouvé.

**Verdict honnête : le bureau accéléré/composé est hors d'atteinte sans écrire un driver Metal (projet type
Asahi, multi‑années). Le meilleur résultat réaliste = macOS HEADLESS + SSH** (l'objectif initial : SSH/auto‑login).

## 3. RUNBOOK — atteindre un macOS utilisable (headless + SSH), avec ce qu'on a

Prérequis matériel confirmé : la **clé USB fait 57,8 Go** ; `D:` (EFI+recovery) = 16 Go ; **~40 Go non alloués**
→ on installe macOS **sur la clé** (Windows du Honor 100 % intact).

### Étape A — Produire un macOS installé (via VM, car l'installeur bare‑metal gèle)
Deux options :
1. **VM sur le Honor (Intel = propre)** : VMware/VirtualBox + installeur Big Sur → installe dans un disque,
   OU passthrough de la clé et installe dans les 40 Go libres.
2. **VM sur le grand PC (AMD Ryzen)** — testé cette session, **ça marche** :
   - Booter depuis `macOS-VM/macOS_Opencore_1.0.4_8_cores.vmdk` (OpenCore avec **19 patches AMD_Vanilla**,
     SMBIOS iMac20,2) en `sata0:0`, l'image macOS en `sata0:1`.
   - **Les patches AMD fonctionnent** (plus de panic « CPU has been disabled »).
   - ⚠️ **BUG à corriger** : cet OpenCore a `Misc>Boot>PickerMode=External` + `UEFI>Output>ProvideConsoleGop`
     non défini → **écran noir** (ni picker ni verbose). **Fix = éditer `EFI/OC/config.plist` du vmdk :
     `PickerMode = Builtin` et `UEFI>Output>ProvideConsoleGop = True`.** (Nécessite un outil de montage FAT :
     OSFMount, ou monter le vmdk, ou reconstruire l'EFI. Je n'ai pas pu le faire en autonomie faute d'outil.)
   - ⚠️ L'image `Olarila-flat.vmdk` présente est probablement **Sequoia** (Metal‑only) → OK pour headless‑SSH,
     PAS pour le bureau OCLP. Pour le bureau, installer **Big Sur** (recovery déjà dispo).

### Étape B — Préparer l'image (dans la VM, avant de la déplacer)
- Créer un compte, **activer Remote Login (SSH)**, **désactiver FileVault**, activer l'auto‑login.
- (Bureau, optionnel) Lancer **OpenCore Legacy Patcher** → root patches **non‑Metal** (pari incertain sur fb nu).

### Étape C — Mettre le macOS sur la clé + booter sur le Honor
- Cloner le volume APFS installé vers une partition dans les **40 Go libres** de la clé USB (asr/CCC, ou
  restaurer l'image sur la partition).
- **Garder NOTRE EFI** (`D:/EFI`, config `config-v3-WORKING.plist`) comme bootloader : il a le fix EBS +
  `HonorFB.kext` + `IOGraphicsFamily.kext` injecté. NE PAS utiliser l'EFI AMD (iMac20,2) sur le Honor.
- Booter sur le Honor : logo Apple → (bureau gèlera à ~50 % = mur Metal, mais) **le reste de l'OS tourne
  headless** → SSH accessible via le réseau.

### Étape D — Accès
- Réseau : Wi‑Fi Intel AX201 → `AirportItlwm.kext` (itlwm) à ajouter, OU adaptateur USB‑C Ethernet, OU
  partage de connexion iPhone en USB.
- `ssh user@<ip>` → **macOS utilisable en ligne de commande** (dev, build, xcodebuild).

## 4. Config « logo Apple » actuelle (sur la clé `D:`)
- SMBIOS MacPro7,1 ; boot‑args `keepsyms=1 debug=0x100` (retirer `-v` = boot graphique).
- Fix EBS : `ProvideCurrentCpuInfo=False` + Cpuid Comet Lake `55 06 0A 00`.
- `Kernel>Add` (ordre) : **IOGraphicsFamily.kext** (injecté, Big Sur v585.1), **HonorFB.kext** (v3+EDID),
  Lilu, VirtualSMC, WhateverGreen, RestrictEvents, CpuTopologyRebuild.
- Sauvegarde de secours : `D:/EFI/OC/config-v3-WORKING.plist`.
- Le driver : repo `variants/` (v0/v1/v2 nub, v3 IOFramebuffer+EDID, sondes vpaint/vreset/vdiag) ; CI GitHub
  Actions builde de vrais `MH_KEXT_BUNDLE` sur runner macOS.

## 5. Ce qu'il ne faut PAS refaire (temps perdu prouvé)
Combos memory‑map, csr 0x800, spoof iGPU, High Sierra, `-igfxvesa` sur Xe, changer la version du recovery pour
passer EBS. Tout ça est écarté avec preuves ci‑dessus.
