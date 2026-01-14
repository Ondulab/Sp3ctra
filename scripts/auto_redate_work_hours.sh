#!/bin/bash

# auto_redate_work_hours.sh - Automatically redate all commits in work hours (8h-18h) to evening/night (19h-23h)
# WARNING: This will rewrite Git history and require force push

set -e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Parse options
DRY_RUN=false
AUTO_PUSH=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        --auto-push)
            AUTO_PUSH=true
            shift
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Usage: $0 [--dry-run] [--auto-push]"
            exit 1
            ;;
    esac
done

print_info "========================================="
print_info "Automatic Work Hours Redate Tool"
print_info "========================================="
print_info "Mode: $([ "$DRY_RUN" = true ] && echo "DRY RUN" || echo "LIVE")"
print_info "Auto-push: $([ "$AUTO_PUSH" = true ] && echo "YES" || echo "NO")"
print_info "========================================="
echo ""

# Check if working directory is clean
if [ "$DRY_RUN" = false ] && [ -n "$(git status --porcelain)" ]; then
    print_error "Working directory is not clean. Please commit or stash your changes."
    git status --short
    exit 1
fi

# Detect all commits in work hours (8h-18h)
print_info "Detecting commits in work hours (08:00-17:59)..."
echo ""

COMMITS_DATA=$(git log --format="%H|%ai|%s" --all | awk -F'|' '
{
    split($2, dt, " ");
    date_part = dt[1];
    time_part = dt[2];
    tz_part = dt[3] " " dt[4];
    
    split(time_part, tm, ":");
    hour = tm[1] + 0;
    minute = tm[2] + 0;
    second = tm[3] + 0;
    
    if (hour >= 8 && hour < 18) {
        # Calculate new evening time (19h-23h)
        # Spread commits across evening hours based on original hour
        if (hour >= 8 && hour < 11) new_hour = 19;
        else if (hour >= 11 && hour < 14) new_hour = 20;
        else if (hour >= 14 && hour < 16) new_hour = 21;
        else if (hour >= 16 && hour < 17) new_hour = 22;
        else new_hour = 23;
        
        new_time = sprintf("%02d:%02d:%02d", new_hour, minute, second);
        
        print $1 "|" date_part " " time_part " " tz_part "|" date_part " " new_time " " tz_part "|" $3;
    }
}')

if [ -z "$COMMITS_DATA" ]; then
    print_success "No commits found in work hours (8h-18h)!"
    exit 0
fi

# Count commits
COMMIT_COUNT=$(echo "$COMMITS_DATA" | wc -l | tr -d ' ')
print_warning "Found $COMMIT_COUNT commits in work hours"
echo ""

# Display first 10 for preview
print_info "Preview (first 10):"
echo "$COMMITS_DATA" | head -10 | while IFS='|' read -r hash old_date new_date message; do
    short_hash=$(echo "$hash" | cut -c1-7)
    old_time=$(echo "$old_date" | cut -d' ' -f2)
    new_time=$(echo "$new_date" | cut -d' ' -f2)
    echo -e "${YELLOW}$short_hash${NC}: $old_time → ${GREEN}$new_time${NC} | $message"
done
echo ""

if [ "$DRY_RUN" = true ]; then
    print_info "[DRY RUN] Would modify $COMMIT_COUNT commits"
    print_info "Full list of commits to modify:"
    echo "$COMMITS_DATA" | while IFS='|' read -r hash old_date new_date message; do
        short_hash=$(echo "$hash" | cut -c1-7)
        old_time=$(echo "$old_date" | cut -d' ' -f2)
        new_time=$(echo "$new_date" | cut -d' ' -f2)
        echo "  $short_hash: $old_time → $new_time"
    done
    exit 0
fi

# Confirmation
print_warning "This will rewrite Git history for $COMMIT_COUNT commits!"
read -p "Do you want to continue? (yes/no): " confirm
if [ "$confirm" != "yes" ]; then
    print_info "Operation cancelled"
    exit 0
fi

# Create backup branch
BACKUP_BRANCH="backup-auto-redate-$(date +%Y%m%d-%H%M%S)"
print_info "Creating backup branch: $BACKUP_BRANCH"
git branch "$BACKUP_BRANCH"
print_success "Backup branch created"
echo ""

# Create filter script
print_info "Preparing git filter-branch operation..."

# Build the filter-branch env-filter script
FILTER_SCRIPT=""
while IFS='|' read -r hash old_date new_date message; do
    FILTER_SCRIPT="${FILTER_SCRIPT}
if [ \"\$GIT_COMMIT\" = \"${hash}\" ]; then
    export GIT_AUTHOR_DATE=\"${new_date}\"
    export GIT_COMMITTER_DATE=\"${new_date}\"
fi"
done <<< "$COMMITS_DATA"

print_info "Filter script prepared with ${COMMIT_COUNT} commit mappings"

# Execute filter-branch
print_info "Executing git filter-branch (this may take a while)..."
echo ""

FILTER_BRANCH_SQUELCH_WARNING=1 git filter-branch -f --env-filter "$FILTER_SCRIPT" --tag-name-filter cat -- --all

# Cleanup
rm -rf .git/refs/original/
print_success "Git history rewritten successfully!"
echo ""

# Verify changes
print_info "Verifying changes..."
REMAINING=$(git log --format="%H|%ai" --all | awk -F'|' '{split($2, dt, " "); split(dt[2], tm, ":"); hour=tm[1]+0; if(hour >= 8 && hour < 18) print $1}' | wc -l | tr -d ' ')

if [ "$REMAINING" -eq 0 ]; then
    print_success "✓ All commits are now outside work hours!"
else
    print_warning "⚠ $REMAINING commits still in work hours (may need another pass)"
fi
echo ""

# Push to remote
if [ "$AUTO_PUSH" = true ]; then
    print_info "Pushing to origin/master..."
    git push --force origin master
    print_success "Pushed to remote"
else
    print_warning "Changes are local only. To push to remote, run:"
    print_info "  git push --force origin master"
fi

echo ""
print_info "Backup branch available: $BACKUP_BRANCH"
print_success "========================================="
print_success "Operation completed!"
print_success "========================================="

exit 0
