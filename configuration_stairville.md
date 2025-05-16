# Configuration pour Stairville Show Bar Tri LED 18x3W RGB

## Informations d'après la documentation

D'après la documentation officielle, cette barre LED offre différents modes DMX :
- Mode 2 canaux : Contrôle simple
- Mode 3 canaux : RGB global
- Mode 5 canaux : RGB + fonctions supplémentaires
- Mode 7 canaux : RGB + fonctions avancées
- Mode 18 canaux : Contrôle par groupes (6 groupes de 3 LEDs)
- Mode 27 canaux : Contrôle avancé par groupes
- Mode 54 canaux : Contrôle individuel de chaque LED

## Modifications réalisées

1. **Adaptation pour 18 zones individuelles**
   - J'ai configuré `DMX_NUM_SPOTS` à 18 dans `config.h` pour gérer les 18 zones individuelles
   - Chaque zone correspond à une LED spécifique de la barre

2. **Configuration des adresses DMX**
   - Configuration optimisée pour le mode 54 canaux (contrôle individuel de chaque LED)
   - Chaque LED individuelle utilise 3 canaux (R, G, B):
     - LED 1: canaux 1-3
     - LED 2: canaux 4-6
     - LED 3: canaux 7-9
     - ... etc.

## Comment configurer votre barre LED Stairville

1. **Sur la barre LED:**
   - Réglez l'adresse DMX de départ à 1
   - Sélectionnez le mode 54 canaux (section correspondante du manuel)
   - Utilisez les boutons du menu de l'appareil pour configurer :
     - Menu → DMX Mode → 54Ch → Confirm

2. **Sur votre interface Enttec DMX:**
   - Assurez-vous que le port DMX dans config.h est correctement configuré:
     ```c
     #define DMX_PORT "/dev/tty.usbserial-AD0JUL0N"
     ```
   - Adaptez ce chemin à l'emplacement réel de votre module ENTTEC sur votre système

## Fonctionnement du mode 54 canaux

Dans ce mode, chaque LED est contrôlée individuellement :
- Canaux 1-3 : RGB pour la LED 1
- Canaux 4-6 : RGB pour la LED 2
- Canaux 7-9 : RGB pour la LED 3
- ... etc.

Le logiciel divise l'image d'entrée en 18 zones, et chaque zone contrôle une LED spécifique. Cela permet d'avoir un effet visuel où chaque LED réagit indépendamment à une portion différente de la ligne d'image, créant un effet de mouvement fluide de gauche à droite.
