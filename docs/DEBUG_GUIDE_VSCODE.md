# Guide de Debug VSCode pour Sp3ctra

## Problème : Breakpoints ignorés dans les applications audio temps réel

Les applications audio temps réel comme Sp3ctra utilisent des threads haute priorité qui peuvent interférer avec le debugger LLDB. Voici les solutions :

## Configurations de Debug Disponibles

### 1. "Debug Sp3ctra (ImagOSCe Debug)" - Configuration principale
- **Usage** : Debug général avec logging détaillé
- **Avantages** : Logging complet, breakpoints dans tous les threads
- **Inconvénients** : Peut être lent avec l'audio temps réel

### 2. "Debug Sp3ctra (Main Thread Only)" - Thread principal uniquement
- **Usage** : Debug du code d'initialisation et du thread principal
- **Avantages** : Évite les conflits avec les threads audio
- **Recommandé pour** : Debug de l'initialisation, configuration, interface

### 3. "Debug Sp3ctra (No Audio)" - Sans audio
- **Usage** : Debug sans système audio actif
- **Avantages** : Pas d'interférence des threads audio
- **Recommandé pour** : Debug de la logique métier, algorithmes

## Stratégies de Debug pour Applications Audio RT

### 1. Debug par Zones
```
┌─────────────────┬─────────────────┬─────────────────┐
│ Zone            │ Configuration   │ Technique       │
├─────────────────┼─────────────────┼─────────────────┤
│ Initialisation  │ Main Thread     │ Breakpoints OK  │
│ Configuration   │ Main Thread     │ Breakpoints OK  │
│ Audio Callback  │ Logging/Printf  │ PAS breakpoints │
│ Interface       │ Main Thread     │ Breakpoints OK  │
│ Algorithmes     │ No Audio        │ Tests unitaires │
└─────────────────┴─────────────────┴─────────────────┘
```

### 2. Debug du Code Audio (Callback RT)
**⚠️ JAMAIS de breakpoints dans les callbacks audio !**

Utilisez plutôt :
```c
// Dans le callback audio - AUTORISÉ
if (debug_condition) {
    // Écriture dans un buffer lock-free
    debug_ring_buffer_write(&debug_buffer, data);
}

// Dans le thread principal - AUTORISÉ
