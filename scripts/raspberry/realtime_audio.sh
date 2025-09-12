#!/bin/bash
# fix_pi_realtime_audio.sh
# Script de configuration automatique pour résoudre la latence audio sur Raspberry Pi
# Corrige les permissions de scheduling temps-réel pour Sp3ctra

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
AUDIO_USER="${AUDIO_USER:-sp3ctra}"
MAX_RT_PRIORITY=75
MAX_MEMLOCK=131072  # 128MB in KB
BACKUP_DIR="/etc/sp3ctra-backup-$(date +%Y%m%d-%H%M%S)"

echo -e "${CYAN}=========================================${NC}"
echo -e "${CYAN}  Sp3ctra Real-Time Audio Fix for Pi   ${NC}"
echo -e "${CYAN}=========================================${NC}"
echo ""

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   echo -e "${RED}❌ Ce script doit être exécuté en tant que root (sudo)${NC}"
   echo "Usage: sudo $0"
   exit 1
fi

# Check if user exists
if ! id "$AUDIO_USER" &>/dev/null; then
    echo -e "${RED}❌ L'utilisateur '$AUDIO_USER' n'existe pas${NC}"
    echo "Créez d'abord l'utilisateur ou définissez AUDIO_USER="
    exit 1
fi

echo -e "${BLUE}🔧 Configuration utilisateur: ${YELLOW}$AUDIO_USER${NC}"
echo -e "${BLUE}🎯 Priorité RT maximale: ${YELLOW}$MAX_RT_PRIORITY${NC}"
echo -e "${BLUE}💾 Mémoire verrouillée max: ${YELLOW}${MAX_MEMLOCK}KB${NC}"
echo ""

# Create backup directory
echo -e "${PURPLE}📁 Création du répertoire de sauvegarde...${NC}"
mkdir -p "$BACKUP_DIR"
echo -e "${GREEN}✅ Sauvegarde dans: $BACKUP_DIR${NC}"
echo ""

# Function to backup file
backup_file() {
    local file="$1"
    if [[ -f "$file" ]]; then
        echo -e "${YELLOW}💾 Sauvegarde de $file${NC}"
        cp "$file" "$BACKUP_DIR/"
    fi
}

# 1. Add user to audio group
echo -e "${BLUE}1️⃣  Configuration des groupes utilisateur${NC}"
echo "--------------------------------------------"

if groups "$AUDIO_USER" | grep -q '\baudio\b'; then
    echo -e "${GREEN}✅ L'utilisateur $AUDIO_USER est déjà dans le groupe audio${NC}"
else
    echo -e "${YELLOW}🔧 Ajout de $AUDIO_USER au groupe audio...${NC}"
    usermod -a -G audio "$AUDIO_USER"
    echo -e "${GREEN}✅ Utilisateur ajouté au groupe audio${NC}"
fi

# Also add to other useful audio-related groups if they exist
for group in pulse pulse-access; do
    if getent group "$group" &>/dev/null; then
        if groups "$AUDIO_USER" | grep -q "\b$group\b"; then
            echo -e "${GREEN}✅ L'utilisateur $AUDIO_USER est déjà dans le groupe $group${NC}"
        else
            echo -e "${YELLOW}🔧 Ajout de $AUDIO_USER au groupe $group...${NC}"
            usermod -a -G "$group" "$AUDIO_USER"
            echo -e "${GREEN}✅ Utilisateur ajouté au groupe $group${NC}"
        fi
    fi
done

echo ""

# 2. Configure /etc/security/limits.conf
echo -e "${BLUE}2️⃣  Configuration des limites de priorité (/etc/security/limits.conf)${NC}"
echo "------------------------------------------------------------------------"

backup_file "/etc/security/limits.conf"

# Check if our configuration already exists
if grep -q "# Sp3ctra Real-Time Audio Configuration" /etc/security/limits.conf; then
    echo -e "${YELLOW}🔄 Configuration Sp3ctra trouvée, mise à jour...${NC}"
    # Remove old configuration
    sed -i '/# Sp3ctra Real-Time Audio Configuration/,/# End Sp3ctra Configuration/d' /etc/security/limits.conf
fi

# Add new configuration
echo -e "${YELLOW}🔧 Ajout de la configuration temps-réel...${NC}"
cat >> /etc/security/limits.conf << EOF

# Sp3ctra Real-Time Audio Configuration
# Added by fix_pi_realtime_audio.sh on $(date)
$AUDIO_USER    soft    rtprio          $MAX_RT_PRIORITY
$AUDIO_USER    hard    rtprio          $MAX_RT_PRIORITY
$AUDIO_USER    soft    memlock         $MAX_MEMLOCK
$AUDIO_USER    hard    memlock         $MAX_MEMLOCK
$AUDIO_USER    soft    nice            -19
$AUDIO_USER    hard    nice            -19
# End Sp3ctra Configuration
EOF

echo -e "${GREEN}✅ Configuration des limites mise à jour${NC}"
echo ""

# 3. Configure systemd for real-time scheduling
echo -e "${BLUE}3️⃣  Configuration systemd (/etc/systemd/system.conf)${NC}"
echo "--------------------------------------------------------"

backup_file "/etc/systemd/system.conf"

# Function to update or add systemd configuration
update_systemd_config() {
    local key="$1"
    local value="$2"
    local file="/etc/systemd/system.conf"
    
    if grep -q "^#*${key}=" "$file"; then
        # Parameter exists (commented or not), update it
        sed -i "s/^#*${key}=.*/${key}=${value}/" "$file"
        echo -e "${YELLOW}🔄 Mis à jour: ${key}=${value}${NC}"
    else
        # Parameter doesn't exist, add it
        echo "${key}=${value}" >> "$file"
        echo -e "${YELLOW}➕ Ajouté: ${key}=${value}${NC}"
    fi
}

echo -e "${YELLOW}🔧 Configuration des paramètres systemd temps-réel...${NC}"

# Add comment header if not exists
if ! grep -q "# Sp3ctra Real-Time Configuration" /etc/systemd/system.conf; then
    cat >> /etc/systemd/system.conf << EOF

# Sp3ctra Real-Time Configuration
# Added by fix_pi_realtime_audio.sh on $(date)
EOF
fi

# Configure systemd parameters for better real-time performance
update_systemd_config "DefaultLimitRTPRIO" "$MAX_RT_PRIORITY"
update_systemd_config "DefaultLimitMEMLOCK" "$MAX_MEMLOCK"
update_systemd_config "DefaultLimitNICE" "-19"

echo -e "${GREEN}✅ Configuration systemd mise à jour${NC}"
echo ""

# 4. Configure ALSA for low-latency (optional)
echo -e "${BLUE}4️⃣  Configuration ALSA (optionnelle)${NC}"
echo "--------------------------------------"

ALSA_CONF="/etc/asound.conf"
if [[ ! -f "$ALSA_CONF" ]]; then
    echo -e "${YELLOW}🔧 Création de la configuration ALSA optimisée...${NC}"
    cat > "$ALSA_CONF" << 'EOF'
# ALSA configuration for low-latency audio
# Created by fix_pi_realtime_audio.sh

# Default PCM device with low-latency settings
pcm.!default {
    type hw
    card 0
    device 0
    period_time 4166  # ~200 frames at 48kHz
    buffer_time 8333  # ~400 frames at 48kHz
}

# Control interface
ctl.!default {
    type hw
    card 0
}
EOF
    echo -e "${GREEN}✅ Configuration ALSA créée${NC}"
else
    echo -e "${GREEN}✅ Configuration ALSA existante conservée${NC}"
fi
echo ""

# 5. Kernel parameter optimization
echo -e "${BLUE}5️⃣  Optimisation des paramètres noyau${NC}"
echo "----------------------------------------"

# Check and configure /etc/sysctl.conf
SYSCTL_CONF="/etc/sysctl.conf"
backup_file "$SYSCTL_CONF"

echo -e "${YELLOW}🔧 Configuration des paramètres noyau pour l'audio temps-réel...${NC}"

# Add kernel parameters for better audio performance
if ! grep -q "# Sp3ctra Audio Optimization" "$SYSCTL_CONF"; then
    cat >> "$SYSCTL_CONF" << 'EOF'

# Sp3ctra Audio Optimization
# Added by fix_pi_realtime_audio.sh
vm.swappiness=10
kernel.sched_rt_runtime_us=-1
kernel.sched_rt_period_us=1000000
EOF
    echo -e "${GREEN}✅ Paramètres noyau ajoutés${NC}"
else
    echo -e "${GREEN}✅ Paramètres noyau déjà configurés${NC}"
fi
echo ""

# 6. Create validation script
echo -e "${BLUE}6️⃣  Création du script de validation${NC}"
echo "------------------------------------"

VALIDATION_SCRIPT="/usr/local/bin/sp3ctra-audio-test"
echo -e "${YELLOW}🔧 Création du script de test audio...${NC}"

cat > "$VALIDATION_SCRIPT" << 'EOF'
#!/bin/bash
# sp3ctra-audio-test
# Script de validation des permissions audio temps-réel pour Sp3ctra

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

USER="${USER:-$LOGNAME}"

echo -e "${BLUE}🧪 Test des permissions audio temps-réel pour $USER${NC}"
echo "=================================================="
echo ""

# Test 1: Group membership
echo -e "${BLUE}1️⃣  Test d'appartenance aux groupes${NC}"
if groups "$USER" | grep -q '\baudio\b'; then
    echo -e "${GREEN}✅ Membre du groupe audio${NC}"
else
    echo -e "${RED}❌ PAS membre du groupe audio${NC}"
    echo -e "${YELLOW}💡 Solution: sudo usermod -a -G audio $USER${NC}"
fi

# Test 2: RT Priority limits
echo -e "${BLUE}2️⃣  Test des limites de priorité temps-réel${NC}"
RT_LIMIT=$(ulimit -r)
if [[ "$RT_LIMIT" != "unlimited" ]] && [[ "$RT_LIMIT" -ge 70 ]]; then
    echo -e "${GREEN}✅ Limite RT Priority: $RT_LIMIT${NC}"
elif [[ "$RT_LIMIT" == "unlimited" ]]; then
    echo -e "${GREEN}✅ Limite RT Priority: illimitée${NC}"
else
    echo -e "${RED}❌ Limite RT Priority insuffisante: $RT_LIMIT${NC}"
    echo -e "${YELLOW}💡 Requis: >= 70${NC}"
fi

# Test 3: Memory lock limits
echo -e "${BLUE}3️⃣  Test des limites de mémoire verrouillée${NC}"
MEMLOCK_LIMIT=$(ulimit -l)
if [[ "$MEMLOCK_LIMIT" != "unlimited" ]] && [[ "$MEMLOCK_LIMIT" -ge 131072 ]]; then
    echo -e "${GREEN}✅ Limite mémoire verrouillée: ${MEMLOCK_LIMIT}KB${NC}"
elif [[ "$MEMLOCK_LIMIT" == "unlimited" ]]; then
    echo -e "${GREEN}✅ Limite mémoire verrouillée: illimitée${NC}"
else
    echo -e "${RED}❌ Limite mémoire verrouillée insuffisante: ${MEMLOCK_LIMIT}KB${NC}"
    echo -e "${YELLOW}💡 Requis: >= 131072KB${NC}"
fi

# Test 4: Nice priority
echo -e "${BLUE}4️⃣  Test des priorités nice${NC}"
NICE_LIMIT=$(ulimit -e)
if [[ "$NICE_LIMIT" -le 19 ]]; then
    echo -e "${GREEN}✅ Limite nice: $NICE_LIMIT${NC}"
else
    echo -e "${RED}❌ Limite nice insuffisante: $NICE_LIMIT${NC}"
    echo -e "${YELLOW}💡 Requis: <= 19 (idéalement -19)${NC}"
fi

echo ""

# Test 5: Practical RT test
echo -e "${BLUE}5️⃣  Test pratique de scheduling temps-réel${NC}"
if command -v chrt >/dev/null 2>&1; then
    if chrt -f 50 true 2>/dev/null; then
        echo -e "${GREEN}✅ Scheduling FIFO (priorité 50) : OK${NC}"
        
        # Test higher priority like Sp3ctra uses
        if chrt -f 70 true 2>/dev/null; then
            echo -e "${GREEN}✅ Scheduling FIFO (priorité 70) : OK${NC}"
            echo -e "${GREEN}🎯 Sp3ctra devrait fonctionner correctement !${NC}"
        else
            echo -e "${YELLOW}⚠️  Scheduling FIFO (priorité 70) : ÉCHEC${NC}"
            echo -e "${YELLOW}💡 Sp3ctra utilisera une priorité plus faible${NC}"
        fi
    else
        echo -e "${RED}❌ Scheduling temps-réel indisponible${NC}"
        echo -e "${YELLOW}💡 Vérifiez la configuration des limites${NC}"
    fi
else
    echo -e "${YELLOW}⚠️  Commande 'chrt' non disponible pour le test${NC}"
fi

echo ""

# Summary
echo -e "${BLUE}📊 RÉSUMÉ${NC}"
echo "----------"
if groups "$USER" | grep -q '\baudio\b' && [[ "$RT_LIMIT" != "0" ]] && [[ "$RT_LIMIT" != "1" ]]; then
    echo -e "${GREEN}✅ Configuration probablement correcte${NC}"
    echo -e "${GREEN}🚀 Redémarrez votre session pour appliquer les changements${NC}"
    echo -e "${BLUE}💡 Testez Sp3ctra : il devrait afficher 'running realtime scheduling'${NC}"
else
    echo -e "${RED}❌ Configuration nécessite des ajustements${NC}"
    echo -e "${YELLOW}💡 Relancez le script fix_pi_realtime_audio.sh${NC}"
fi

echo ""
echo -e "${BLUE}🔍 Pour plus d'infos : journalctl -f pendant le lancement de Sp3ctra${NC}"
EOF

chmod +x "$VALIDATION_SCRIPT"
echo -e "${GREEN}✅ Script de validation créé: $VALIDATION_SCRIPT${NC}"
echo ""

# 7. Final diagnostics and recommendations
echo -e "${BLUE}7️⃣  Diagnostic final et recommandations${NC}"
echo "----------------------------------------"

echo -e "${PURPLE}📋 RÉSUMÉ DES MODIFICATIONS :${NC}"
echo "• Utilisateur $AUDIO_USER ajouté au groupe audio"
echo "• Limites RT configurées dans /etc/security/limits.conf"
echo "• Paramètres systemd optimisés dans /etc/systemd/system.conf" 
echo "• Configuration ALSA créée/vérifiée"
echo "• Paramètres noyau optimisés dans /etc/sysctl.conf"
echo "• Script de validation installé : $VALIDATION_SCRIPT"
echo ""

echo -e "${YELLOW}⚠️  ACTIONS REQUISES :${NC}"
echo "1. ${BLUE}Redémarrez le système${NC} pour appliquer tous les changements :"
echo "   sudo reboot"
echo ""
echo "2. ${BLUE}Après redémarrage${NC}, connectez-vous comme $AUDIO_USER et testez :"
echo "   $VALIDATION_SCRIPT"
echo ""
echo "3. ${BLUE}Lancez Sp3ctra${NC} et vérifiez le message :"
echo "   './build/Sp3ctra' | grep -i 'realtime'"
echo "   ${GREEN}Succès${NC} : pas de message '_NOT_'"
echo "   ${RED}Échec${NC} : message 'RtAudio alsa: _NOT_ running realtime scheduling'"
echo ""

echo -e "${GREEN}🎯 OBJECTIF ATTEINT :${NC}"
echo "• Priorité réduite de 90 → 70 dans le code (plus compatible)"
echo "• Permissions système configurées pour supporter priorité 75"
echo "• Latence audio considérablement réduite attendue"
echo ""

echo -e "${PURPLE}💾 Sauvegardes créées dans : $BACKUP_DIR${NC}"
echo -e "${PURPLE}🔧 Pour annuler les changements, restaurez depuis cette sauvegarde${NC}"
echo ""

echo -e "${CYAN}=========================================${NC}"
echo -e "${CYAN}     Configuration terminée avec succès  ${NC}"
echo -e "${CYAN}=========================================${NC}"
