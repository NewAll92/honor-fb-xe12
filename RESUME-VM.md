# REPRISE — Projet macOS Honor MagicBook X16 (i5-12450H, Xe Gen12)

> But final : **driver d'affichage pour l'iGPU Xe Gen12** → macOS bare-metal sur le Honor avec
> image à l'écran interne. Recette EFI complète : `../BUILD-RECIPE.md`.

## ✅ DRIVER — 3 variantes buildées en vrais kexts (CI)
Repo **github.com/NewAll92/honor-fb-xe12** (gh = NewAll92). CI matrice macOS →
3 kexts **MH_KEXT_BUNDLE** (filetype 11 vérifié), artefacts dans `HonorFB/built-kexts/` :
- **v0-baseline** : clone fidèle acidanthera/UEFIGraphicsFB (nub `display` + deviceMemory=PE_state.video).
- **v1-devicetree** : + présence dans `gIODTPlane`, `device_type=display`, `AAPL,boot-display`, masque bits bas.
- **v2-ioresources** : match `IOResources` (exécution garantie) + **synthèse explicite** du nub `display`.
Sources : `HonorFB/variants/v{0,1,2}-*/` (driver.cpp + kmod_info.c + Info.plist). Voir `variants/README.md`.
Hypothèse de chaque variante = un mode d'échec différent du binding IONDRVFramebuffer.

## ✅ MÉDIA DE BOOT BARE-METAL — clé USB Kingston (Disque 1, 57,8 Go)
Décision (2026-07-02) : **abandon de l'install en VM** (VMware a son GPU → ne teste pas le driver ;
et l'iPhone via VMware = passage USB instable). On passe **bare-metal** = le vrai objectif + vrai test.

Clé = **U:/ (ou D:) FAT32 « OPENCORE » 16 Go**, contenu :
- `/EFI` = OpenCore 1.0.7 (variante C auditée) : iGPU Xe **neutralisé**
  (`PciRoot(0x0)/Pci(0x2,0x0)` name=`<unused>`, class-code=FFFFFFFF), **HonorFB.kext injecté** (= v0),
  `ScanPolicy=0`, `DmgLoading=Signed`, `SecureBootModel=Disabled`, `-v`, SMBIOS `MacPro7,1`.
- `/com.apple.recovery.boot` = **Recovery Apple authentique** (BaseSystem.dmg 884 Mo + chunklist,
  **vérifiée chunklist** via macrecovery.py). Version ≤ Sequoia (idéale pour tester le framebuffer).
- `/kext-variants` = les 3 kexts pour **swapper** (remplacer `EFI/OC/Kexts/HonorFB.kext` par v1 puis v2
  en gardant le nom `HonorFB.kext` — la config pointe dessus).
- `/RUNBOOK-HONOR.txt` = **procédure pas-à-pas complète** (BIOS, boot, test driver, install 140 Go).

### Le test du driver = immédiat, dès la Recovery
Boot OpenCore (GOP → menu visible) → « macOS Recovery ». Texte verbeux = normal. **LE TEST** : quand
la Recovery passe à l'UI graphique → écran ALLUMÉ = HonorFB marche 🎉 ; écran NOIR = swap variante suivante.

### Install (si écran OK)
iPhone branché **direct** sur le Honor (pas VMware) → trust + partage → Recovery a le réseau
(`ipconfig set en1 DHCP` si besoin) → Utilitaire de disque : créer APFS **dans les 140 Go libres UNIQUEMENT**
(NE JAMAIS toucher Windows ~340 Go) → « Réinstaller macOS ». Puis SSH ON, auto-login ON, FileVault OFF.

## macOS 26 (Tahoe) — cible finale
La Recovery actuelle est ≤ Sequoia (choisie exprès : WindowServer y accepte un framebuffer « bête »).
**Stratégie** : 1) prouver que l'écran s'allume (Sequoia/≤) ; 2) ensuite viser macOS 26. ⚠️ Le
WindowServer de 26 peut EXIGER Metal → le framebuffer seul pourrait ne pas suffire (à valider).
Pour une Recovery 26 : `python macrecovery.py -b Mac-27AD2F918AE68F61 -os latest download` (host a internet).

## Outils / chemins clés
- `scratchpad/macrecovery.py` (OpenCorePkg) — télécharge Recovery Apple signée+vérifiée.
- `scratchpad/usb-staging/` — arbre exact copié sur la clé (source de vérité).
- `scratchpad/vmvnc.py` — pilotage VNC de la VM (⚠️ clavier VM = AZERTY : double-mapping,
  chiffres via Shift ; lettres i/f/c/o/n/g/s/e/t identiques).
- VM `macOS-Honor.vmx` : conservée comme fallback (recovery Sequoia bootable), mais plus la voie principale.

## ⚠️ Pièges rencontrés
- Écriture clé USB : le gros `BaseSystem.dmg` (843 Mo) → **copier en tâche de fond** (sinon timeout 2 min
  qui coupe et **corrompt la FAT**). Sérialiser les copies, pas d'écriture concurrente.
- Sandbox Windows bloque `Remove-Item`/écrasement sur certaines lettres (ex. `D:`) → reformater propre
  (copies de fichiers NEUFS OK) ou `dangerouslyDisableSandbox` ciblé.
- iPhone via VMware = interface `en1` qui apparaît/disparaît, pas de DHCP sans trust → non fiable. Direct sur le Honor = mieux.

## Rollback sécurité Windows (fin de projet)
`bcdedit /set hypervisorlaunchtype auto` + DeviceGuard EnableVirtualizationBasedSecurity=1 + HVCI Enabled=1, reboot.
