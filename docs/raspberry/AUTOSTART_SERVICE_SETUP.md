# Configuration de Démarrage Automatique - Sp3ctra

Ce document décrit la configuration complète du service systemd pour le démarrage automatique de l'application Sp3ctra sur Raspberry Pi.

## Vue d'ensemble

L'application Sp3ctra utilise **systemd** pour le démarrage automatique au boot du système. Le service est configuré pour :
- Démarrer automatiquement après le réseau et les services audio
- Redémarrer automatiquement en cas d'erreur
- Fonctionner en mode headless (sans interface graphique)
- Logger tous les messages dans le journal système

## Structure des fichiers de configuration

### 1. Fichier service principal
**Emplacement :** `/etc/systemd/system/sp3ctra-synth.service`

```ini
[Unit]
Description=Sp3ctra Synth (headless)
Wants=network-online.target
After=network-online.target sound.target

[Service]
Type=simple
User=sp3ctra
Group=sp3ctra
WorkingDirectory=/home/sp3ctra/Sp3ctra_Application
ExecStart=/home/sp3ctra/Sp3ctra_Application/build/Sp3ctra
Restart=on-failure
RestartSec=2
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

### 2. Fichier override (personnalisation)
**Emplacement :** `/etc/systemd/system/sp3ctra-synth.service.d/override.conf`

```ini
[Service]
ExecStart=
ExecStart=/home/sp3ctra/Sp3ctra_Application/build/Sp3ctra
```

## Explication de la configuration

### Section [Unit]
- `Description` : Description du service
- `Wants=network-online.target` : Service souhaité mais non critique
- `After=network-online.target sound.target` : Démarre après le réseau et l'audio

### Section [Service]
- `Type=simple` : Service en avant-plan
- `User=sp3ctra` : Utilisateur d'exécution
- `WorkingDirectory` : Répertoire de travail de l'application
- `ExecStart` : Commande à exécuter
- `Restart=on-failure` : Redémarre en cas d'erreur
- `RestartSec=2` : Délai avant redémarrage (2 secondes)
- `StandardOutput/Error=journal` : Logs vers journald

### Section [Install]
- `WantedBy=multi-user.target` : Active au démarrage du système

## Commandes de gestion du service

### Vérifier l'état du service
```bash
sudo systemctl status sp3ctra-synth.service
```

### Activer/désactiver le démarrage automatique
```bash
# Activer
sudo systemctl enable sp3ctra-synth.service

# Désactiver
sudo systemctl disable sp3ctra-synth.service

# Vérifier si activé
sudo systemctl is-enabled sp3ctra-synth.service
```

### Contrôler le service
```bash
# Démarrer
sudo systemctl start sp3ctra-synth.service

# Arrêter
sudo systemctl stop sp3ctra-synth.service

# Redémarrer
sudo systemctl restart sp3ctra-synth.service

# Recharger la configuration
sudo systemctl daemon-reload
```

### Consulter les logs
```bash
# Logs en temps réel
sudo journalctl -u sp3ctra-synth.service -f

# Logs depuis le boot
sudo journalctl -u sp3ctra-synth.service -b

# Dernières 50 lignes
sudo journalctl -u sp3ctra-synth.service -n 50
```

## Diagnostic et dépannage

### Vérifications courantes

1. **Service activé ?**
   ```bash
   sudo systemctl is-enabled sp3ctra-synth.service
   ```

2. **Service en cours d'exécution ?**
   ```bash
   sudo systemctl is-active sp3ctra-synth.service
   ```

3. **Exécutable existant ?**
   ```bash
   ls -la /home/sp3ctra/Sp3ctra_Application/build/Sp3ctra
   ```

4. **Permissions correctes ?**
   ```bash
   # L'exécutable doit être exécutable
   chmod +x /home/sp3ctra/Sp3ctra_Application/build/Sp3ctra
   ```

### Problèmes courants

#### Service ne démarre pas au boot
1. Vérifier que le service est activé :
   ```bash
   sudo systemctl enable sp3ctra-synth.service
   ```

2. Vérifier les dépendances :
   ```bash
   systemd-analyze critical-chain sp3ctra-synth.service
   ```

#### Échecs de démarrage
1. Examiner les logs d'erreur :
   ```bash
   sudo journalctl -u sp3ctra-synth.service --no-pager
   ```

2. Tester l'exécutable manuellement :
   ```bash
   sudo -u sp3ctra /home/sp3ctra/Sp3ctra_Application/build/Sp3ctra
   ```

#### Redémarrages fréquents
1. Augmenter le délai de redémarrage dans le service :
   ```ini
   RestartSec=5
   ```

2. Vérifier les logs pour identifier la cause :
   ```bash
   sudo journalctl -u sp3ctra-synth.service | grep -i error
   ```

## Maintenance et mise à jour

### Modification de la configuration

1. **Éditer le fichier principal :**
   ```bash
   sudo systemctl edit --full sp3ctra-synth.service
   ```

2. **Créer/modifier un override :**
   ```bash
   sudo systemctl edit sp3ctra-synth.service
   ```

3. **Recharger après modification :**
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl restart sp3ctra-synth.service
   ```

### Mise à jour de l'application

1. **Arrêter le service :**
   ```bash
   sudo systemctl stop sp3ctra-synth.service
   ```

2. **Mettre à jour l'exécutable :**
   ```bash
   cd /home/sp3ctra/Sp3ctra_Application
   git pull
   make clean && make
   ```

3. **Redémarrer le service :**
   ```bash
   sudo systemctl start sp3ctra-synth.service
   ```

### Changement de chemin d'installation

Si le projet est déplacé, mettre à jour les chemins dans le service :

1. **Éditer la configuration :**
   ```bash
   sudo systemctl edit sp3ctra-synth.service
   ```

2. **Modifier les chemins :**
   ```ini
   [Service]
   WorkingDirectory=/nouveau/chemin/vers/Sp3ctra_Application
   ExecStart=
   ExecStart=/nouveau/chemin/vers/Sp3ctra_Application/build/Sp3ctra
   ```

3. **Recharger et redémarrer :**
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl restart sp3ctra-synth.service
   ```

## Sécurité et bonnes pratiques

### Utilisateur dédié
- Le service s'exécute sous l'utilisateur `sp3ctra` (non-root)
- Cet utilisateur doit avoir accès aux périphériques audio et USB nécessaires

### Permissions minimales
- L'exécutable doit être accessible en lecture/exécution
- Le répertoire de travail doit être accessible en lecture

### Logs et monitoring
- Tous les logs sont centralisés via journald
- Utiliser `logrotate` si les logs deviennent volumineux
- Surveiller la santé du service avec `systemctl status`

## Historique des modifications

- **v1.0** : Configuration initiale avec chemin `/home/sp3ctra/Sp3ctra/`
- **v1.1** : Correction du chemin vers `/home/sp3ctra/Sp3ctra_Application/`
- **v1.2** : Suppression des arguments `--audio-device=1 --no-dmx` pour compatibilité boot

---

**Note :** Cette documentation est maintenue pour le projet Sp3ctra. En cas de modification de la configuration, mettre à jour ce document en conséquence.
