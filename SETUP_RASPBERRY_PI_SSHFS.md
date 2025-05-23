# Préparation et Configuration d'un Raspberry Pi avec SSHFS sur macOS

Ce document décrit les étapes pour préparer une image système pour un Raspberry Pi, installer les outils nécessaires sur macOS et configurer SSHFS afin de monter un répertoire distant du Raspberry Pi.

## Préparation de l'image système pour Raspberry Pi (Lite OS)

Cette section décrit comment préparer une image de Raspberry Pi OS Lite pour un Raspberry Pi 5 en utilisant l'outil Raspberry Pi Imager, en configurant l'accès SSH et la connexion Wi-Fi.

1.  **Télécharger et lancer Raspberry Pi Imager:**
    *   Téléchargez la dernière version de [Raspberry Pi Imager](https://www.raspberrypi.com/software/) pour votre système d'exploitation.
    *   Installez et lancez l'application.

2.  **Configurer l'image :**
    *   **Appareil Raspberry Pi :** Dans Raspberry Pi Imager, cliquez sur "CHOISIR L'APPAREIL" et sélectionnez "Raspberry Pi 5".
    *   **Système d'exploitation :** Cliquez sur "CHOISIR L'OS". Sélectionnez "Raspberry Pi OS (other)" puis "Raspberry Pi OS Lite (64-bit)" (recommandé pour Pi 5) ou "Raspberry Pi OS Lite (32-bit)".
    *   **Stockage :** Cliquez sur "CHOISIR LE STOCKAGE" et sélectionnez la carte SD ou le périphérique USB sur lequel vous souhaitez écrire l'image.

3.  **Personnaliser les paramètres :**
    *   Cliquez sur "SUIVANT" puis sur "MODIFIER LES RÉGLAGES" (ou cherchez une icône d'engrenage pour les versions plus anciennes de l'Imager).
    *   **Général :**
        *   **Définir le nom d'hôte :** `pi.local` (Cochez la case correspondante).
        *   **Définir le nom d'utilisateur et le mot de passe :**
            *   Nom d'utilisateur : `sp3ctra`
            *   Mot de passe : `FB5FA76AC3` (Il est fortement recommandé de changer ce mot de passe après la première connexion pour des raisons de sécurité).
        *   **Configurer le Wi-Fi :**
            *   Cochez "Configurer le réseau sans fil".
            *   SSID : `PRE_WIFI_5GHZ`
            *   Mot de passe : `FB5FA76AC3`
            *   Pays du Wi-Fi : Sélectionnez votre pays (par ex., FR pour la France).
        *   **Paramètres de localisation :** Configurez le fuseau horaire (par ex., `Europe/Paris`) et la disposition du clavier (par ex., `fr`) selon vos besoins.
    *   **Services :**
        *   **Activer SSH :** Cochez "Activer SSH".
    *   **Préconfiguration avec une clé SSH spécifique :**
        *   Dans l'onglet "Services" (ou une section similaire pour SSH), collez votre clé SSH publique dans le champ approprié. L'option peut être nommée "Autoriser l'authentification par clé publique" ou similaire. Cela permet une connexion sécurisée sans mot de passe dès le premier démarrage. Si cette méthode ne fonctionne pas immédiatement, la configuration manuelle via `ssh-copy-id` (décrite plus loin) sera nécessaire après la première connexion par mot de passe.
        *   Clé publique à utiliser :
            ```
            ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAACAQCocWE1PWyMOjUN0G9TnIvZKXL6IN82Gp2OZ9w+BEC9V/ze+CrEKVS+E7uxrfEiLFCdGbDJzrCgF6erBVua+3XMpF2I2BQWySK25WgHPCOL+b/4KMCM9/vpao1hDtDAegdmNiHsAsl9Lzm1UOtwvjGw8GmZFftwILl0B4zq7Oer98yU5WN6wnGMMWwutKRq3S72/dYBOIYE/3JAa4KRDim5XbIg3z9sbFFS8w4s0bp9toaIR5q0B15iUglTsIMWihD+c8aT7ukMy3u35UYdc7vjajVQ2x/cGDyADaML8U89bgLVSv7UIVX8bzyRgEvIDpBynTEMtK7gkoa0PxMYt7JZjGHo8fLBJsi6T1TiFw8sCQmxfIQrAT0eWyxiFnDiPzjFFi/FLvHvkvpcr/8+jwtJgKbVMBX54UVso6tLD96Rej3/Gn56VXJzOFtONWa8Tq5E6yxLnDP8/LBZE7Iagjmpmmy7/AkGDRyMnDy6DCyhHVUwdT3MOi0ZsDk5+j/AuY/VxYLEojShM/Y4MbSConsd/PTX0svGCBKeSbdkoNFh00WwuAHEnuEhfXxYrAtvKDJoqmyb0aeTfkMZJRPKDO77sgM5Ev5/jqARLO0NIi4wEPpXXxWFqngVvTMCsH0om+kpcw5OX0DZGt9vCEAWwyRbKiL0KEhXYuc5qGGGepZTtw== patrick.reybaud@electro-crea.com
            ```
    *   Cliquez sur "ENREGISTRER".

4.  **Écrire l'image :**
    *   De retour sur l'écran principal de l'Imager, cliquez sur "OUI" pour appliquer les personnalisations de l'OS.
    *   Cliquez sur "OUI" pour confirmer que vous souhaitez effacer toutes les données existantes sur le périphérique de stockage.
    *   Attendez la fin du processus d'écriture et de vérification.

5.  **Premier démarrage du Raspberry Pi :**
    *   Éjectez en toute sécurité la carte SD (ou le périphérique USB) de votre ordinateur et insérez-la dans votre Raspberry Pi 5.
    *   Connectez l'alimentation. Le Pi devrait démarrer et se connecter automatiquement au réseau Wi-Fi configuré.
    *   Après quelques minutes, vous devriez pouvoir vous connecter via SSH en utilisant le nom d'hôte et les informations d'identification que vous avez configurés (par exemple, `ssh sp3ctra@pi.local`).

## Configuration de SSHFS pour monter un répertoire Raspberry Pi sur macOS

### Prérequis sur macOS

-   Un Mac avec Homebrew installé.

### Étapes d'installation et de configuration sur macOS

1.  **Installer macFUSE:**
    macFUSE est nécessaire pour permettre aux systèmes de fichiers en espace utilisateur de fonctionner sur macOS.

    ```bash
    brew install macfuse
    ```
    *Remarque : L'installation de macFUSE peut nécessiter une approbation dans les Préférences Système (Sécurité et confidentialité) pour charger l'extension noyau.*

2.  **Installer SSHFS:**
    SSHFS (SSH Filesystem) est un client de système de fichiers basé sur FUSE pour monter des répertoires distants via une connexion SSH.

    ```bash
    brew install gromgit/fuse/sshfs
    ```
    *Remarque : La formule `sshfs-mac` de `gromgit/fuse` est utilisée ici. Il est mentionné qu'elle est obsolète et pourrait être supprimée à l'avenir si des problèmes de sécurité sont découverts.*

3.  **Créer un point de montage local:**
    Créez un répertoire sur votre Mac qui servira de point de montage pour le répertoire distant du Raspberry Pi.

    ```bash
    mkdir ~/pi_mount
    ```
    *Si le répertoire existe déjà, la commande `mkdir` échouera, ce qui est normal.*

4.  **Transférer les clés SSH sur le Raspberry Pi et configurer les permissions (Méthode manuelle) :**
    Si la préconfiguration de la clé SSH via Raspberry Pi Imager n'a pas fonctionné ou si vous avez besoin de le faire manuellement :

    a.  **Copier les clés SSH (privée et publique) sur le Raspberry Pi :**
        Depuis votre Mac, utilisez `scp` pour transférer vos fichiers `id_rsa` (clé privée) et `id_rsa.pub` (clé publique) dans le répertoire `.ssh` de l'utilisateur `sp3ctra` sur le Raspberry Pi.
        ```bash
        # Assurez-vous que le répertoire .ssh existe sur le Pi pour l'utilisateur sp3ctra
        # ssh sp3ctra@pi.local "mkdir -p /home/sp3ctra/.ssh" 
        # (Exécutez cette commande si le répertoire n'existe pas, après une première connexion par mot de passe)

        scp ~/.ssh/id_rsa sp3ctra@pi.local:/home/sp3ctra/.ssh/
        scp ~/.ssh/id_rsa.pub sp3ctra@pi.local:/home/sp3ctra/.ssh/
        ```
        *Remarque : Vous serez invité à entrer le mot de passe de `sp3ctra` sur le Raspberry Pi pour ces commandes `scp` si la connexion par clé n'est pas encore active.*

    b.  **Définir les permissions correctes sur le Raspberry Pi :**
        Connectez-vous au Raspberry Pi via SSH (initialement avec le mot de passe si nécessaire) :
        ```bash
        ssh sp3ctra@pi.local
        ```
        Une fois connecté, exécutez les commandes suivantes pour définir les permissions appropriées pour le répertoire `.ssh` et les fichiers de clés :
        ```bash
        # Sur le Raspberry Pi
        chmod 700 ~/.ssh
        chmod 600 ~/.ssh/id_rsa
        chmod 644 ~/.ssh/id_rsa.pub 
        # Si vous prévoyez d'utiliser la clé publique pour l'authentification sur le Pi lui-même (authorized_keys)
        # cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys
        # chmod 600 ~/.ssh/authorized_keys 
        ```
        Après avoir défini ces permissions, la connexion SSH sans mot de passe devrait fonctionner.

5.  **Monter le répertoire distant avec SSHFS:**
    Utilisez SSHFS pour monter le répertoire `/home/sp3ctra` du Raspberry Pi sur le point de montage `~/pi_mount` créé précédemment sur votre Mac.
    ```bash
    sshfs sp3ctra@pi.local:/home/sp3ctra ~/pi_mount
    ```
## Connexion SSH et clonage du dépôt sur le Raspberry Pi

Une fois que la clé SSH est copiée (ou si vous utilisez l'authentification par mot de passe initialement), vous pouvez vous connecter au Raspberry Pi :

1.  **Se connecter au Raspberry Pi via SSH:**
    Si vous avez copié la clé SSH :
    ```bash
    ssh -i ~/.ssh/id_rsa -o IdentitiesOnly=yes sp3ctra@pi.local
    ```
    Si vous utilisez le mot de passe configuré dans Raspberry Pi Imager :
    ```bash
    ssh sp3ctra@pi.local
    ```
    Cela établit une connexion SSH.

2.  **Cloner le dépôt Git sur le Raspberry Pi:**
    Une fois connecté au Raspberry Pi, naviguez jusqu'au répertoire souhaité et clonez votre dépôt.

    ```bash
    # Sur le Raspberry Pi, après la connexion SSH
    git clone git@github.com:Ondulab/Sp3ctra_Application.git
    ```

## Pour démonter le répertoire

Lorsque vous avez terminé, vous pouvez démonter le répertoire distant sur votre Mac :

```bash
umount ~/pi_mount
```

Cette documentation couvre les étapes pour préparer un Raspberry Pi, configurer SSHFS sur macOS et interagir avec le Raspberry Pi.
