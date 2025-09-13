# Guide de Connexion SSH - Sp3ctra

Ce document décrit la configuration et l'utilisation de SSH pour se connecter au Raspberry Pi dans le cadre du projet Sp3ctra, incluant la résolution des problèmes courants.

## Vue d'ensemble

Le projet Sp3ctra utilise SSH pour l'administration à distance du Raspberry Pi. L'utilisateur principal est `sp3ctra` avec l'hôte configuré en `pi.local` via mDNS.

## Configuration SSH de base

### Connexion standard
```bash
# Connexion interactive
ssh sp3ctra@pi.local

# Exécution de commande distante
ssh sp3ctra@pi.local "sudo systemctl status sp3ctra-synth.service"

# Connexion avec vérification stricte des clés
ssh -o StrictHostKeyChecking=ask sp3ctra@pi.local
```

### Configuration recommandée ~/.ssh/config

Créer ou modifier le fichier `~/.ssh/config` :

```
Host sp3ctra-pi
    HostName pi.local
    User sp3ctra
    Port 22
    StrictHostKeyChecking ask
    UserKnownHostsFile ~/.ssh/known_hosts
    ServerAliveInterval 60
    ServerAliveCountMax 3
```

Utilisation avec la configuration :
```bash
ssh sp3ctra-pi
```

## Résolution du problème "Remote Host Identification Changed"

### Description de l'erreur

Lorsque vous rencontrez ce message :

```
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!
Someone could be eavesdropping on you right now (man-in-the-middle attack)!
It is also possible that a host key has just been changed.
The fingerprint for the ED25519 key sent by the remote host is
SHA256:gndi8kIwFvfD3ONTyoLu6CVhqepqFRXozUBNwoyWlYw.
Please contact your system administrator.
Add correct host key in /Users/username/.ssh/known_hosts to get rid of this message.
Offending ED25519 key in /Users/username/.ssh/known_hosts:15
Host key for pi.local has changed and you have requested strict checking.
Host key verification failed.
```

### Causes courantes

1. **Réinstallation du Raspberry Pi OS**
2. **Changement d'adresse IP du Pi**
3. **Régénération des clés SSH sur le Pi**
4. **Remplacement de la carte SD**
5. **Mise à jour système ayant régénéré les clés**

### Solutions

#### Solution 1 : Suppression automatique avec ssh-keygen (recommandée)

```bash
# Supprimer l'ancienne clé d'hôte
ssh-keygen -R pi.local

# Puis se reconnecter
ssh -o StrictHostKeyChecking=ask sp3ctra@pi.local
```

#### Solution 2 : Édition manuelle du fichier known_hosts

```bash
# Identifier la ligne problématique (indiquée dans le message d'erreur)
# Dans l'exemple ci-dessus : ligne 15
nano ~/.ssh/known_hosts

# Supprimer la ligne correspondante et sauvegarder
```

#### Solution 3 : Suppression complète du fichier known_hosts (non recommandée)

```bash
# ATTENTION : Supprime TOUTES les clés d'hôtes connues
rm ~/.ssh/known_hosts
```

### Processus de reconnexion

1. **Supprimer l'ancienne clé :**
   ```bash
   ssh-keygen -R pi.local
   ```

2. **Se reconnecter avec vérification :**
   ```bash
   ssh -o StrictHostKeyChecking=ask sp3ctra@pi.local
   ```

3. **Accepter la nouvelle clé :**
   Quand le message apparaît :
   ```
   The authenticity of host 'pi.local (2a02:8429:4d8c:4601:791d:f9bf:431f:e9d9)' can't be established.
   ED25519 key fingerprint is SHA256:gndi8kIwFvfD3ONTyoLu6CVhqepqFRXozUBNwoyWlYw.
   This key is not known by any other names.
   Are you sure you want to continue connecting (yes/no/[fingerprint])?
   ```
   
   Répondre : `yes`

4. **Vérifier la connexion :**
   ```bash
   ssh sp3ctra@pi.local "echo 'Connexion SSH fonctionnelle'"
   ```

## Options SSH utiles

### Options de sécurité

```bash
# Connexion avec vérification stricte des clés
ssh -o StrictHostKeyChecking=ask sp3ctra@pi.local

# Connexion sans vérification (NON RECOMMANDÉ pour la production)
ssh -o StrictHostKeyChecking=no sp3ctra@pi.local

# Connexion avec timeout personnalisé
ssh -o ConnectTimeout=10 sp3ctra@pi.local
```

### Options de diagnostic

```bash
# Connexion en mode verbose pour diagnostic
ssh -v sp3ctra@pi.local

# Mode très verbose
ssh -vvv sp3ctra@pi.local

# Tester la connexion sans ouvrir de session
ssh -o BatchMode=yes sp3ctra@pi.local exit
```

## Commandes courantes pour Sp3ctra

### Administration système

```bash
# Vérifier l'état du service
ssh sp3ctra@pi.local "sudo systemctl status sp3ctra-synth.service"

# Redémarrer le service
ssh sp3ctra@pi.local "sudo systemctl restart sp3ctra-synth.service"

# Consulter les logs
ssh sp3ctra@pi.local "sudo journalctl -u sp3ctra-synth.service -n 20"

# Vérifier l'espace disque
ssh sp3ctra@pi.local "df -h"

# Vérifier la charge système
ssh sp3ctra@pi.local "uptime"
```

### Développement et déploiement

```bash
# Tester l'exécutable
ssh sp3ctra@pi.local "ls -la /home/sp3ctra/Sp3ctra_Application/build/Sp3ctra"

# Vérifier les processus en cours
ssh sp3ctra@pi.local "ps aux | grep Sp3ctra"

# Redémarrer le Pi
ssh sp3ctra@pi.local "sudo reboot"

# Arrêter le Pi proprement
ssh sp3ctra@pi.local "sudo shutdown -h now"
```

## Diagnostic et dépannage

### Vérification des clés d'hôte

```bash
# Voir les clés d'hôte connues pour pi.local
grep pi.local ~/.ssh/known_hosts

# Voir toutes les clés d'hôte
cat ~/.ssh/known_hosts

# Vérifier la clé d'hôte actuelle du serveur
ssh-keyscan pi.local
```

### Problèmes courants

#### 1. Connexion refused

```bash
# Erreur : Connection refused
# Solutions possibles :
# - Vérifier que SSH est activé sur le Pi
# - Vérifier l'adresse réseau du Pi
ping pi.local
```

#### 2. Timeout de connexion

```bash
# Erreur : Connection timed out
# Solutions :
# - Vérifier la connectivité réseau
# - Augmenter le timeout
ssh -o ConnectTimeout=30 sp3ctra@pi.local
```

#### 3. Permission denied

```bash
# Erreur : Permission denied (publickey,password)
# Solutions :
# - Vérifier le nom d'utilisateur
# - Vérifier que l'authentification par mot de passe est activée
ssh -o PreferredAuthentications=password sp3ctra@pi.local
```

### Tests de connectivité

```bash
# Test de résolution DNS
nslookup pi.local

# Test de ping
ping -c 4 pi.local

# Test de port SSH
nc -zv pi.local 22

# Test SSH avec timeout court
timeout 5 ssh sp3ctra@pi.local exit
```

## Sécurité et bonnes pratiques

### Recommandations de sécurité

1. **Utiliser StrictHostKeyChecking=ask** pour détecter les changements de clés
2. **Sauvegarder le fichier ~/.ssh/known_hosts** régulièrement
3. **Vérifier les fingerprints** lors de la première connexion
4. **Utiliser des clés SSH** plutôt que des mots de passe quand possible
5. **Monitorer les connexions SSH** dans les logs du Pi

### Configuration sécurisée du Pi

Sur le Raspberry Pi, vérifier `/etc/ssh/sshd_config` :

```bash
ssh sp3ctra@pi.local "sudo cat /etc/ssh/sshd_config | grep -E '^(PermitRootLogin|PasswordAuthentication|PubkeyAuthentication)'"
```

Configuration recommandée :
- `PermitRootLogin no`
- `PasswordAuthentication yes` (ou `no` si clés SSH configurées)
- `PubkeyAuthentication yes`

## Scripts utiles

### Script de vérification de connexion

Créer un script `scripts/raspberry/test_ssh_connection.sh` :

```bash
#!/bin/bash
echo "Testing SSH connection to Sp3ctra Pi..."

# Test de ping
if ping -c 1 pi.local >/dev/null 2>&1; then
    echo "✓ Pi is reachable via network"
else
    echo "✗ Pi is not reachable"
    exit 1
fi

# Test SSH
if timeout 10 ssh -o BatchMode=yes sp3ctra@pi.local exit >/dev/null 2>&1; then
    echo "✓ SSH connection successful"
else
    echo "✗ SSH connection failed"
    exit 1
fi

# Test service
if ssh sp3ctra@pi.local "sudo systemctl is-active sp3ctra-synth.service" | grep -q "active"; then
    echo "✓ Sp3ctra service is running"
else
    echo "⚠ Sp3ctra service is not running"
fi

echo "SSH connection test completed."
```

### Script de nettoyage des clés d'hôte

Créer un script `scripts/raspberry/reset_ssh_host_key.sh` :

```bash
#!/bin/bash
echo "Resetting SSH host key for pi.local..."

# Supprimer l'ancienne clé
ssh-keygen -R pi.local

echo "Host key removed. Next SSH connection will prompt for key verification."
echo "To connect: ssh -o StrictHostKeyChecking=ask sp3ctra@pi.local"
```

## Historique et changelog

- **v1.0** : Documentation initiale avec résolution du problème host key changed
- **v1.1** : Ajout des scripts utiles et options de diagnostic
- **v1.2** : Amélioration des bonnes pratiques de sécurité

---

**Note :** Cette documentation est maintenue pour le projet Sp3ctra. En cas de modification de la configuration réseau ou SSH, mettre à jour ce document en conséquence.

## Références

- [OpenSSH Manual](https://www.openssh.com/manual.html)
- [SSH Config File Documentation](https://linux.die.net/man/5/ssh_config)
- [Raspberry Pi SSH Documentation](https://www.raspberrypi.org/documentation/remote-access/ssh/)
