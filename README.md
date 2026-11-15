# Contrôles multimédias pour LXQt Panel

**Projet généré avec l’IA (OpenAI Codex).**

Widget natif C++/Qt qui pilote les lecteurs multimédias MPRIS par D-Bus. Un clic gauche lance ou met en pause la lecture. La molette change de piste. Quand plusieurs lecteurs sont ouverts, le widget privilégie celui qui joue.

Le menu **Configurer « Contrôles multimédias »** permet de choisir les icônes, leur taille, la largeur ou la hauteur du widget et l’action de chaque geste.

## Installation

Télécharge un fichier `.deb` compatible dans les [releases](../../releases).

| Environnement | Versions ciblées | Architectures | Éditeurs de liens |
| --- | --- | --- | --- |
| GNU/Linux, ABI Ubuntu 24.04 | LXQt Panel 1.4, Qt 5.15, glibc 2.39 | amd64, i386, arm64, armhf, riscv64, ppc64el, s390x | GNU ld.bfd, GNU gold, LLVM LLD, mold. RISC-V sans gold. |
| Termux natif, préfixe standard `com.termux` | LXQt Panel 2.5.1, Qt 6.12, Android API 24+, Bionic | aarch64, arm, i686, x86_64 | LLD du NDK Android r30 |

Sur GNU/Linux, `lxqt-panel-mediacontrol-gnu_*_all.deb` contient les variantes des sept architectures. LXQt charge la variante GNU ld.bfd depuis le répertoire correspondant à son architecture. Les autres variantes sont aussi disponibles en paquets individuels. L’architecture i386 requiert une installation compatible de LXQt Panel 1.4, absente des dépôts Ubuntu 24.04 pour cette architecture.

```sh
# GNU/Linux, paquet commun
pkexec apt install ./lxqt-panel-mediacontrol-gnu_1.1.1_all.deb

# GNU/Linux, exemple de paquet individuel
pkexec apt install ./lxqt-panel-mediacontrol-mold_1.1.1_amd64.deb

# Termux arm64 natif, dépôt X11 activé
apt install ./lxqt-panel-mediacontrol-lld_1.1.1_aarch64.deb
```

Un seul paquet du widget doit être installé à la fois. Après installation, redémarre LXQt Panel, ouvre **Configurer le panneau → Widgets → +**, puis ajoute **Contrôles multimédias**. Si le widget est déjà présent, le panneau recharge sa configuration existante.

Les quatre variantes amd64 ont été chargées dans LXQt Panel et leur boîte de réglages a été ouverte. Pour les autres architectures, les vérifications portent sur le format ELF, les dépendances et les symboles. Les paquets Termux ciblent le préfixe natif. Sous proot, un panneau GNU/Linux utilise le paquet correspondant à l’ABI de la distribution invitée.

## Compilation

Utilise les en-têtes et la version majeure de Qt de ton LXQt Panel. Pour Qt 6, remplace `-DLXQT_MEDIA_QT_MAJOR=5` par `-DLXQT_MEDIA_QT_MAJOR=6`. Le répertoire de bibliothèques doit correspondre à celui utilisé par ton panneau.

```sh
nice -n 19 ionice -c 3 cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DLXQT_MEDIA_QT_MAJOR=5 -DCMAKE_INSTALL_PREFIX=/usr
nice -n 19 ionice -c 3 cmake --build build --parallel 1
```

## Licence

Le code du widget est distribué sous [licence MIT](LICENSE).
