# Branchement du récepteur IR sur Raspberry Pi

## Composants nécessaires

1. **Récepteur IR** (ex: TSOP4838, VS1838B, ou similaire)
2. **3 fils de connexion** (jumper wires)
3. **Breadboard** (optionnel mais recommandé)

## Schéma de branchement

### Récepteur IR TSOP4838 / VS1838B
```
Récepteur IR (vue de face, LED vers vous):
    OUT  VCC  GND
     |    |    |
     |    |    |
GPIO18  3.3V  GND
```

### Tableau de connexions
| Pin récepteur IR | Pin Raspberry Pi | Couleur fil suggérée |
|------------------|------------------|---------------------|
| VCC (alimentation) | Pin 1 (3.3V)    | Rouge              |
| GND (masse)       | Pin 6 (GND)     | Noir               |
| OUT (signal)      | Pin 12 (GPIO18) | Jaune/Blanc        |

## Branchement détaillé

1. **Coupez l'alimentation** du Raspberry Pi

2. **Connectez VCC** :
   - Pin VCC du récepteur → Pin 1 (3.3V) du Pi
   - ⚠️ **ATTENTION** : Utilisez 3.3V, pas 5V !

3. **Connectez GND** :
   - Pin GND du récepteur → Pin 6 (GND) du Pi

4. **Connectez OUT** :
   - Pin OUT du récepteur → Pin 12 (GPIO18) du Pi

## Configuration du Pi

1. **Activez le support LIRC** dans `/boot/config.txt` :
   ```bash
   sudo nano /boot/config.txt
   ```
   
   Ajoutez ces lignes :
   ```
   # Configuration LIRC
   dtoverlay=gpio-ir,gpio_pin=18
   dtoverlay=gpio-ir-tx,gpio_pin=19
   ```

2. **Redémarrez** le Pi :
   ```bash
   sudo reboot
   ```

3. **Vérifiez le device** :
   ```bash
   ls -la /dev/lirc*
   # Devrait afficher /dev/lirc0 et /dev/lirc1
   ```

## Test du récepteur

1. **Test basique** :
   ```bash
   mode2 -d /dev/lirc0
   ```
   
2. **Pointez votre télécommande** vers le récepteur et appuyez sur POWER

3. **Vous devriez voir** des lignes comme :
   ```
   pulse 2400
   space 600
   pulse 1200
   space 600
   ...
   ```

## Disposition GPIO du Raspberry Pi
```
     3.3V  (1) (2)  5V
    GPIO2  (3) (4)  5V
    GPIO3  (5) (6)  GND  ← Connexion GND
    GPIO4  (7) (8)  GPIO14
      GND  (9) (10) GPIO15
   GPIO17 (11) (12) GPIO18  ← Connexion OUT
   GPIO27 (13) (14) GND
   GPIO22 (15) (16) GPIO23
     3.3V (17) (18) GPIO24
   GPIO10 (19) (20) GND
    GPIO9 (21) (22) GPIO25
   GPIO11 (23) (24) GPIO8
      GND (25) (26) GPIO7
    GPIO0 (27) (28) GPIO1
    GPIO5 (29) (30) GND
    GPIO6 (31) (32) GPIO12
   GPIO13 (33) (34) GND
   GPIO19 (35) (36) GPIO16  
   GPIO26 (37) (38) GPIO20
      GND (39) (40) GPIO21
```

## Alternative : Utilisation d'un autre GPIO

Si GPIO18 est occupé, modifiez dans `/boot/config.txt` :
```
dtoverlay=gpio-ir,gpio_pin=XX
```
(Remplacez XX par le numéro GPIO de votre choix)

## Dépannage

**Problème** : Aucun signal détecté
- Vérifiez les connexions
- Testez avec une autre télécommande
- Vérifiez que le récepteur n'est pas défaillant

**Problème** : Device /dev/lirc0 introuvable
- Vérifiez `/boot/config.txt`
- Redémarrez le Pi
- Vérifiez avec `dmesg | grep lirc`

Une fois le récepteur branché et testé, vous pourrez capturer les vrais codes de votre télécommande Sony !
