# Configuration DMX pour Raspberry Pi - Guide Complet

## Vue d'ensemble

Ce guide explique comment résoudre le problème de communication DMX sur Raspberry Pi avec l'adaptateur FT232 USB-Serial.

## Problème identifié

### Symptômes
- Message d'erreur : `Error opening serial port: No such file or directory`
- Aucune lumière DMX ne s'allume sur Raspberry Pi
- Baud rate incorrect affiché dans les logs (13 au lieu de 250000)

### Cause racine
1. **Port série incorrect** : Configuration macOS (`/dev/tty.usbserial-AD0JUL0N`) incompatible avec Linux
2. **Baud rate imprécis** : Utilisation de B230400 au lieu du 250000 exact requis par DMX

## Solution implémentée

### 1. Règle udev pour lien symbolique permanent

**Fichier:** `scripts/raspberry/99-sp3ctra-dmx.rules`
```bash
SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", SYMLINK+="sp3ctra-dmx", GROUP="dialout", MODE="0664", TAG+="uaccess"
```

**Avantages:**
- Lien symbolique stable `/dev/sp3ctra-dmx`
- Indépendant de l'ordre de branchement USB
- Permissions correctes automatiquement

### 2. Configuration baud rate précis avec termios2

**Implémentation:**
- Fonction `set_custom_baud_rate_linux()` utilisant `termios2`
- Baud rate exact de 250000 Hz (requis par DMX)
- Fallback vers constantes B* standard si termios2 échoue

### 3. Configuration multi-plateforme

**Configuration automatique:**
```c
#ifdef __APPLE__
#define DMX_PORT "/dev/tty.usbserial-AD0JUL0N"
#else
#define DMX_PORT "/dev/sp3ctra-dmx"
#endif
```

## Installation

### Étape 1: Installer la règle udev

```bash
cd Sp3ctra_Application
sudo scripts/raspberry/install_dmx_udev.sh
```

Le script va :
- Installer la règle udev dans `/etc/udev/rules.d/`
- Recharger les règles udev
- Vérifier que le lien symbolique est créé
- Afficher le statut de la configuration

### Étape 2: Recompiler l'application

```bash
make clean && make
```

### Étape 3: Tester le DMX

```bash
./build/Sp3ctra
```

**Logs attendus sur Raspberry Pi :**
```
DMX baud rate 250000 configuré avec précision via termios2
Serial port opened and configured successfully.
```

## Vérification

### Vérifier le lien symbolique
```bash
ls -la /dev/sp3ctra-dmx
# Doit afficher: lrwxrwxrwx 1 root root 7 Dec  9 20:40 /dev/sp3ctra-dmx -> ttyUSB0
```

### Vérifier l'adaptateur USB
```bash
lsusb | grep "0403:6001"
# Doit afficher: Bus 003 Device 003: ID 0403:6001 Future Technology Devices International, Ltd FT232 Serial (UART) IC
```

### Tester la communication série
```bash
# Test de base (optionnel)
sudo minicom -b 250000 -D /dev/sp3ctra-dmx
```

## Dépannage

### Problème: Lien symbolique non créé

**Vérifications:**
```bash
# 1. Adaptateur détecté ?
lsusb | grep "0403:6001"

# 2. Port série disponible ?
ls -la /dev/ttyUSB*

# 3. Règle udev active ?
cat /etc/udev/rules.d/99-sp3ctra-dmx.rules

# 4. Test manuel de la règle
sudo udevadm test /sys/class/tty/ttyUSB0
```

**Solutions:**
- Débrancher/rebrancher l'adaptateur USB
- Redémarrer le service udev : `sudo systemctl restart udev`
- Vérifier les permissions utilisateur dans le groupe `dialout`

### Problème: Baud rate incorrect

**Symptômes:**
- Logs affichent "B250000 non disponible, utilisation de B230400"
- DMX ne fonctionne pas malgré le port correct

**Vérification:**
```bash
# Vérifier si termios2 est disponible
grep -r "termios2" /usr/include/
```

**Solution:**
- Mettre à jour le kernel Linux
- Vérifier que les headers linux sont installés

### Problème: Permissions insuffisantes

**Symptômes:**
- `Error opening serial port: Permission denied`

**Solution:**
```bash
# Ajouter l'utilisateur au groupe dialout
sudo usermod -a -G dialout $USER

# Se déconnecter/reconnecter pour appliquer
```

## Architecture technique

### Détection automatique du système
```c
#ifdef __APPLE__
    // Configuration macOS avec IOSSIOSPEED
#else
    // Configuration Linux avec termios2 + fallback
#endif
```

### Hiérarchie des configurations baud rate
1. **termios2** : Baud rate personnalisé exact (250000)
2. **B250000** : Constante standard (si disponible)
3. **B230400** : Constante proche (fallback)
4. **B38400** : Fallback ultime

### Structure des trames DMX
- Start code : 0x00
- 512 canaux de données
- Baud rate : 250000 bps exactement
- 8 data bits, 2 stop bits, no parity

## Tests de validation

### Test de base
```bash
./build/Sp3ctra
# Vérifier: "Serial port opened and configured successfully."
```

### Test avec données
Envoyer des images via UDP pour voir les lumières réagir :
```bash
# (Depuis un autre terminal/machine)
# Envoyer une image de test au port UDP 55151
```

### Test de performance
```bash
# Vérifier la latence DMX
# Les updates doivent être fluides à ~44fps
```

## Maintenance

### Logs de débogage
```bash
# Logs udev
journalctl -f | grep udev

# Logs série
dmesg | grep ttyUSB

# Logs application
./build/Sp3ctra --verbose
```

### Mise à jour de la règle udev
```bash
# Modifier la règle si nécessaire
sudo nano /etc/udev/rules.d/99-sp3ctra-dmx.rules

# Recharger
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## Spécifications matérielles

### Adaptateur recommandé
- **Modèle** : FT232 USB-Serial
- **Vendor ID** : 0403 (FTDI)
- **Product ID** : 6001
- **Compatible** : Raspberry Pi 4, 3B+, Zero 2 W

### Spots DMX supportés
- **Stairville Show Bar Tri LED 18x3W RGB**
- **Mode** : 54 canaux (contrôle individuel 18 LEDs)
- **Adresses** : 1-54 (RGB pour chaque LED)
- **Protocole** : DMX-512 standard

## Historique des modifications

- **v1.0** : Configuration macOS uniquement
- **v1.1** : Support Linux basique avec B230400
- **v1.2** : **Solution complète** avec termios2, udev et baud rate exact

La solution DMX est maintenant opérationnelle et robuste sur Raspberry Pi ! 🎉
