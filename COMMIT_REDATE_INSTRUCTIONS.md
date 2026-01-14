# Instructions pour modifier les dates des commits

Les commits suivants doivent être modifiés pour avoir des heures de nuit (hors 8h-18h):

## Commits à modifier:

1. `feebee9187` - 2025-11-25 10:13:08 → **22:00:00**
2. `4d36314ba8` - 2025-11-25 10:13:59 → **22:30:00**
3. `59ff0cd5dc` - 2025-11-25 10:31:27 → **23:00:00**
4. `5dd5a8fbd5` - 2025-11-25 11:31:33 → **23:30:00**

## Méthode manuelle (la plus fiable):

```bash
# 1. Créer une branche de backup
git branch backup-manual-redate-$(date +%Y%m%d-%H%M%S)

# 2. Utiliser git filter-branch pour réécrire toute l'historique
git filter-branch --env-filter '
if [ "$GIT_COMMIT" = "feebee9187fef96f987ec0b84b9b2cc886e9b128" ]; then
    export GIT_AUTHOR_DATE="2025-11-25 22:00:00 +0100"
    export GIT_COMMITTER_DATE="2025-11-25 22:00:00 +0100"
fi
if [ "$GIT_COMMIT" = "4d36314ba8121ac1b04939daef0578fb474cfad2" ]; then
    export GIT_AUTHOR_DATE="2025-11-25 22:30:00 +0100"
    export GIT_COMMITTER_DATE="2025-11-25 22:30:00 +0100"
fi
if [ "$GIT_COMMIT" = "59ff0cd5dc4595b19112354d038b591dbfcaebd7" ]; then
    export GIT_AUTHOR_DATE="2025-11-25 23:00:00 +0100"
    export GIT_COMMITTER_DATE="2025-11-25 23:00:00 +0100"
fi
if [ "$GIT_COMMIT" = "5dd5a8fbd58ce28cce30cd048d7b44ad504224d8" ]; then
    export GIT_AUTHOR_DATE="2025-11-25 23:30:00 +0100"
    export GIT_COMMITTER_DATE="2025-11-25 23:30:00 +0100"
fi
' --tag-name-filter cat -- --all

# 3. Nettoyer les refs de backup
rm -rf .git/refs/original/

# 4. Vérifier les modifications
git log --format="%h %ai %s" | grep -E "(photowave|i18n|midi|synth.*distinguish)"

# 5. Force push vers GitHub
git push --force-with-lease origin master
```

## Rollback si nécessaire:

```bash
# Revenir à la branche de backup
git reset --hard backup-manual-redate-XXXXXXXX-XXXXXX
```

## Notes importantes:

- ⚠️ Cette opération réécrit TOUT l'historique Git
- ⚠️ Tous les commits après seront modifiés (nouveaux hashes)
- ⚠️ Le force push est obligatoire
- ✅ Une branche de backup est créée avant toute modification
