# Configuration IR pour Sony HT-X8500

## Problème résolu

Le fichier `sony_power.ir` original contenait de mauvais codes IR. Les nouveaux fichiers utilisent les codes hexadécimaux corrects pour le protocole Sony SIRC.

## Codes IR corrects

- **Power**: `0x54`
- **Volume Up**: `0x24`
- **Volume Down**: `0x64`
- **Mute**: `0x14`

## Test rapide

Pour tester immédiatement le nouveau code Power :

```bash
ir-ctl -d /dev/lirc0 --carrier=40000 --duty-cycle=33 --send=sony_power_new.ir
```

## Utilisation du script de test

1. Copier tous les fichiers sur votre Raspberry Pi
2. Rendre le script exécutable :
   ```bash
   chmod +x test_sony_ir.sh
   ```
3. Tester toutes les commandes :
   ```bash
   ./test_sony_ir.sh
   ```
4. Ou tester une commande spécifique :
   ```bash
   ./test_sony_ir.sh power
   ./test_sony_ir.sh volume_up
   ```

## Configuration LIRC (recommandée)

Pour une utilisation plus pratique, configurez LIRC :

1. **Copier le fichier de configuration :**
   ```bash
   sudo cp sony_ht_x8500.lircd.conf /etc/lirc/lircd.conf.d/
   ```

2. **Redémarrer LIRC :**
   ```bash
   sudo systemctl restart lircd
   ```

3. **Tester avec irsend :**
   ```bash
   irsend SEND_ONCE Sony_HT_X8500 POWER
   irsend SEND_ONCE Sony_HT_X8500 VOLUME_UP
   irsend SEND_ONCE Sony_HT_X8500 MUTE
   ```

## Dépannage

Si les commandes ne fonctionnent toujours pas :

1. **Vérifier que la LED IR clignote :**
   ```bash
   ir-ctl -d /dev/lirc0 --carrier=40000 --duty-cycle=33 --send=sony_power_new.ir
   ```

2. **Tester avec différents nombres de répétitions :**
   ```bash
   for i in {1..5}; do ir-ctl -d /dev/lirc0 --carrier=40000 --duty-cycle=33 --send=sony_power_new.ir; sleep 0.3; done
   ```

3. **Vérifier la distance :** Placez l'émetteur IR à moins de 2 mètres de la barre de son

4. **Mode d'apprentissage :** Si les codes estimés pour les modes sonores ne marchent pas, utilisez `irrecord` pour les apprendre depuis la vraie télécommande

## Codes supplémentaires disponibles

Le fichier LIRC inclut des codes estimés pour :
- INPUT (changement de source)
- CINEMA, MUSIC, GAME, NEWS, SPORT (modes sonores)
- VOICE (clarté des dialogues)

Ces codes peuvent nécessiter un ajustement via apprentissage IR.

## Protocole technique

- **Protocole :** Sony SIRC 12-bit
- **Fréquence porteuse :** 40 kHz
- **Duty cycle :** 33%
- **Format timing :** Header 2400µs, bits 600µs/1200µs
