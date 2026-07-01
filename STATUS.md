# HonorFB.kext — Journal de bord (session autonome)

**Projet :** écrire un kext `HonorFB.kext` qui sous-classe `IOFramebuffer` et expose le
framebuffer de boot (GOP, déjà allumé par le firmware) à WindowServer, pour afficher
l'image sur le Honor MagicBook X16 (i5-12450H, iGPU Intel Xe Gen12 **sans driver macOS**).
Injecté via OpenCore `Kernel/Add` dans l'installeur macOS Sequoia 15.

**But de la session :** compiler le kext **sans macOS** (carte blanche, utilisateur absent).

---

## Environnement de la machine (constaté)

| Élément | État |
|---|---|
| OS | Windows 11, PC fixe Ryzen 7 5700X + RTX 3080 |
| Admin | ❌ NON (session non élevée) |
| WSL2 | ❌ BLOQUÉ — `HCS_E_HYPERV_NOT_INSTALLED` (Plateforme d'ordi virtuel off / virtu BIOS). Non activable sans admin+BIOS+reboot. |
| Docker | ❌ (dépend de WSL2/Hyper-V, bloqué) |
| clang/LLVM | ❌ absent → téléchargement portable en cours |
| gh (GitHub CLI) | ❌ absent |
| git | ✅ 2.49 (user ScanVerseFrance) |
| winget | ✅ 1.29 (user scope) |
| 7-Zip | ✅ (extraction sans admin) |
| Disque C | ✅ 187 Go libres |

## Conséquence sur la stratégie de build

- **osxcross / WSL2** → mort en autonome (WSL2 bloqué, pas admin).
- **Route A (retenue, autonome) :** LLVM portable natif Windows (clang `-fapple-kext` +
  headers SDK/KDK extraits) → au minimum **compiler/valider le code**. Le link final en
  `MH_KEXT_BUNDLE` est le point à risque (dépend du support kext par `ld64.lld`).
- **Route B (fallback prêt-à-l'emploi) :** workflow **GitHub Actions** (runner macOS =
  vrai Xcode) → build fiable du kext. Non poussé en autonome (pas de gh/token) ; livré
  prêt, l'utilisateur pousse au retour.

---

## Journal (horodaté à la fin de session)

- [start] Env sondé. WSL2 bloqué. Recherche technique lancée. Téléchargement LLVM lancé.
- [+] LLVM 22.1.8 win64 téléchargé (434 Mo) + extrait via 7-Zip → `tools/llvm/bin/`
  (clang.exe, clang++.exe, ld64.lld.exe, llvm-nm, llvm-ar...). **Sans admin, OK.**
- [+] `clang 22.1.8` vérifié : cross-cible `x86_64-apple-macos13`, `-fapple-kext` accepté
  (C++/ObjC++). Compile-to-`.o` fonctionnel → on pourra **valider le code du kext** en autonome.
- [!] **BLOCAGE LINK (décisif) :** `ld64.lld` **n'implémente pas `-kext`** — warning
  *"Option `-kext' is undocumented"* et sortie `MH_EXECUTE` (filetype `02`), PAS
  `MH_KEXT_BUNDLE` (`0B`). Testé empiriquement.
  → **Impossible de produire un vrai `.kext` avec lld sur Windows.** Il faut le `ld64` de
  cctools (Unix/WSL2 — bloqué) ou le `ld` Apple (macOS/GitHub Actions).
- **Plan ajusté :**
  1. autonome : récupérer SDK+KDK → **écrire le kext** → **compiler en `.o`** (valider) ;
  2. livrer un **workflow GitHub Actions** (runner macOS = vrai `ld64`) prêt à pousser ;
  3. tenter un hack expérimental (link `-bundle` puis patch filetype→`0B`) — non garanti ;
  4. documenter l'option WSL2/cctools si l'utilisateur active la virtualisation.
- [+] **Recherche technique revenue.** Découvertes clés :
  - **MacKernelSDK** (`acidanthera/MacKernelSDK`, public) suffit pour les headers noyau
    (IOKit, pexpert, libkmod) → **pas besoin du KDK/Apple ID** pour compiler.
  - Reco forte : tester d'abord des chemins SANS code (`-igfxvesa`, UEFIGraphicsFB) — voir
    variantes EFI. Le kext custom = approche B (nub `IOPlatformDevice` "display", pas un
    sous-classement d'IOFramebuffer).
  - **UEFIGraphicsFB a un binaire RELEASE prêt** (acidanthera 1.0.0).
- [+] **Driver HonorFB écrit** : `src/HonorFB.cpp` (approche B) + `src/kmod_info.c` +
  `HonorFB.kext/Contents/Info.plist` + `.github/workflows/build-kext.yml` (CI macOS).
- [✅] **COMPILE VALIDÉ EN AUTONOME** (LLVM natif + MacKernelSDK, zéro macOS/Apple ID) :
  `kmod_info.o` (856 o) + `HonorFB.o` (26 592 o), symboles IOKit corrects
  (`HonorFB::start(IOService*)`, `gMetaClass`, MetaClass ctor/dtor). ABI `-fapple-kext` OK.
- [!] **LINK : lld ne fait pas de vrai kext.** `ld64.lld -kext` → warning "undocumented" +
  `MH_EXECUTE`. Contournement tenté : `-bundle -undefined dynamic_lookup` (link OK, 49 Ko,
  `_kmod_info` présent) + patch filetype `08→0B`. MAIS load commands = **`LC_DYLD_INFO_ONLY`**
  (binding dyld userspace) au lieu des relocations classiques attendues par `kxld` →
  **binaire probablement NON chargeable.** Livré comme `HonorFB.kext` **EXPÉRIMENTAL**.
  → **Voie fiable pour un vrai `.kext` = GitHub Actions** (`.github/workflows/build-kext.yml`,
  runner macOS = vrai `ld64` Apple qui implémente `-kext`). Pousser le repo, récupérer l'artefact.
- [✅] **3 variantes EFI générées + validées `ocvalidate` (No issues found)** dans
  `HonorFB/efi-variants/` :
  - `Honor-EFI-A-igfxvesa` — boot-arg `igfxvesa` (WhateverGreen force l'iGPU en VESA).
    **Zéro kext custom. Le chemin le plus PROUVÉ pour Intel bare-metal.**
  - `Honor-EFI-B-uefigraphicsfb` — kext UEFIGraphicsFB (acidanthera, prêt) + iGPU neutralisé
    (`class-code=FFFFFFFF`). Le framebuffer de boot exposé à WindowServer.
  - `Honor-EFI-C-honorfb-EXPERIMENTAL` — notre `HonorFB.kext` (lld, **peut ne pas charger**)
    + iGPU neutralisé. À remplacer par le kext buildé en CI.

---

## AU RETOUR — quoi faire (ordre recommandé)

**But : obtenir une IMAGE à l'écran du Honor. Aucune de ces variantes n'a pu être testée
par moi (pas de boot possible à distance). Odds recherche : ~60-70% qu'une marche.**

1. **Teste `Honor-EFI-A-igfxvesa` D'ABORD** (le plus probable, zéro kext).
   Copie `efi-variants/Honor-EFI-A-igfxvesa/EFI` sur la partition EFI de l'USB
   (même manip admin qu'avant), boote, regarde si le **bureau/installeur s'affiche**.
2. Si A échoue → **`Honor-EFI-B-uefigraphicsfb`** (UEFIGraphicsFB + iGPU neutralisé).
3. Si tu veux TON driver : **build le vrai `HonorFB.kext` via GitHub Actions**
   (le `.github/workflows/build-kext.yml` est prêt ; pousse le dossier `HonorFB/` dans un
   repo GitHub → onglet Actions → télécharge l'artefact `HonorFB.kext` → remplace celui de
   la variante C → teste). Le kext expérimental lld livré ne chargera probablement pas
   (relocations `LC_DYLD_INFO` ≠ ce que `kxld` attend).
4. Si tu actives la virtualisation (BIOS) + WSL2 (admin), je peux monter osxcross/cctools
   pour builder le kext en local (voir §TOOLCHAIN du rapport de recherche).

**Rappel honnête :** même si une image s'affiche, ce sera **software-rendered = lent**
(pas d'accélération, impossible sur Xe). Le piège n°1 = SkyLight qui reboot WindowServer si
un accélérateur s'attache → d'où la neutralisation iGPU en B/C. Pour A, c'est l'inverse :
on laisse WhateverGreen gérer l'iGPU en VESA.

## Arborescence livrée (`C:\Users\samym\Downloads\HonorFB\`)
```
src/HonorFB.cpp          driver approche B (COMPILE OK)
src/kmod_info.c          bookkeeping kmod (COMPILE OK)
HonorFB.kext/            bundle EXPÉRIMENTAL (lld, non vérifié)
build/HonorFB.o          objet compilé validé
.github/workflows/build-kext.yml   CI macOS = vrai .kext
MacKernelSDK/            headers noyau (clone public)
tools/llvm/              clang/lld portables
tools/UEFIGraphicsFB/    kext prêt acidanthera
efi-variants/A,B,C       EFI de test validés ocvalidate
STATUS.md                ce journal
```

