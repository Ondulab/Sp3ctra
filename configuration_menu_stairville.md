# Guide de configuration du menu de la Stairville Show Bar Tri LED 18x3W RGB

## Présentation du panneau de contrôle

Le panneau de contrôle de votre barre LED se compose de :
- Un afficheur à 7 segments (4 caractères)
- Quatre boutons de contrôle : MODE, SETUP, UP et DOWN

## Navigation dans les menus

### Boutons de contrôle
- **MODE** : Accéder au menu principal / Changer de mode / Revenir au menu précédent
- **SETUP** : Confirmer une sélection / Entrer dans un sous-menu
- **UP** : Naviguer vers le haut dans les options du menu / Augmenter une valeur
- **DOWN** : Naviguer vers le bas dans les options du menu / Diminuer une valeur

## Configuration de l'adresse DMX

D'après la documentation, section "Adresse DMX" (page 24) :

1. Appuyez sur le bouton **MODE** à plusieurs reprises jusqu'à ce que l'écran affiche `Addr`
2. Appuyez sur **SETUP** pour accéder au réglage
3. L'afficheur montre une valeur entre `1` et `512`
4. Utilisez les boutons **UP** et **DOWN** pour régler l'adresse à `001`
5. Appuyez sur **SETUP** pour confirmer votre sélection
6. Patientez environ 30 secondes jusqu'à ce que l'écran s'éteigne pour que les réglages soient enregistrés

## Configuration du mode DMX 54 canaux

Pour sélectionner le mode 54 canaux, après avoir configuré l'adresse DMX :

1. Appuyez sur le bouton **MODE** à plusieurs reprises jusqu'à ce que l'écran affiche `ChMd` (Channel Mode)
2. Appuyez sur **SETUP** pour y accéder
3. Utilisez les boutons **UP** et **DOWN** pour naviguer jusqu'à l'option `54Ch` (mode 54 canaux)
4. Appuyez sur **SETUP** pour confirmer
5. Patientez environ 30 secondes jusqu'à ce que l'écran s'éteigne pour que les réglages soient enregistrés

## Retour au menu principal

Pour revenir au menu supérieur sans modification, appuyez sur **MODE**

## Structure complète du menu

Voici la structure complète du menu pour référence :

```
Menu Principal
├── Addr (Adresse DMX)
│   └── 001-512 (Régler sur 001)
├── ChMd (Mode de canaux DMX)
│   ├── 2Ch (2 canaux)
│   ├── 3Ch (3 canaux)
│   ├── 5Ch (5 canaux)
│   ├── 7Ch (7 canaux)
│   ├── 18Ch (18 canaux)
│   ├── 27Ch (27 canaux)
│   └── 54Ch (54 canaux) ← SÉLECTIONNER CELUI-CI
├── SLMd (Mode esclave)
│   ├── MAST (Maître)
│   └── SL (Esclave)
├── ShMd (Mode spectacle)
│   ├── CoLo (Changement de couleur)
│   ├── Fade (Fondu de couleur)
│   └── Stro (Stroboscope)
├── SouN (Mode sonore)
│   ├── On (Activé)
│   └── Off (Désactivé)
├── SEnS (Sensibilité du microphone)
│   └── 000-100 (Sensibilité)
└── SYS (Paramètres système)
    ├── Led (Écran toujours allumé ou non)
    ├── DISP (Inversion de l'affichage)
    ├── REST (Réinitialisation)
    └── VER (Version du firmware)
```

## Notes importantes

- Si l'afficheur s'éteint après un certain temps d'inactivité, appuyez sur n'importe quel bouton pour le réactiver
- Après environ 30 secondes sans activité, l'appareil revient automatiquement au mode de fonctionnement normal
- Pour que le mode DMX fonctionne correctement, assurez-vous que le contrôleur DMX est correctement connecté et que l'appareil est en mode DMX (non en mode autonome)
- Le mode 54 canaux permet de contrôler individuellement chaque LED, ce qui est idéal pour créer des effets où chaque LED réagit à une portion différente de l'image
