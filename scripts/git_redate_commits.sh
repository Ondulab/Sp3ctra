#!/bin/bash

# git_redate_commits.sh - Modify commit dates in Git history
# This script rewrites Git history to change commit timestamps
# WARNING: This will rewrite history and require force push

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
AUTO_PUSH=false
DRY_RUN=false
KEEP_BACKUP=false
BACKUP_BRANCH=""
ORIGINAL_BRANCH=""

# Function to print colored messages
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

# Function to display usage
usage() {
    cat << EOF
Usage: $0 --commits "hash1,hash2,..." --times "HH:MM,HH:MM,..." [OPTIONS]

Modify the dates of specific Git commits to new times (keeping the same date).

Required arguments:
  --commits HASHES    Comma-separated list of commit hashes (short or full)
  --times TIMES       Comma-separated list of times in HH:MM format (24h)

Optional arguments:
  --auto-push         Automatically push to remote after successful rebase
  --dry-run           Show what would be done without actually doing it
  --keep-backup       Keep the backup branch after successful operation
  -h, --help          Display this help message

Examples:
  # Modify 4 commits to night hours
  $0 --commits "feebee9187,4d36314ba8,59ff0cd5dc,5dd5a8fbd5" \\
     --times "22:00,22:30,23:00,23:30"

  # Dry run to see what would happen
  $0 --commits "abc123,def456" --times "07:00,07:30" --dry-run

  # Auto-push and keep backup
  $0 --commits "abc123" --times "19:00" --auto-push --keep-backup

EOF
    exit 1
}

# Function to validate time format
validate_time() {
    local time=$1
    if [[ ! $time =~ ^[0-2][0-9]:[0-5][0-9]$ ]]; then
        print_error "Invalid time format: $time (expected HH:MM)"
        return 1
    fi
    local hour=${time%%:*}
    if [ "$hour" -gt 23 ]; then
        print_error "Invalid hour: $hour (must be 00-23)"
        return 1
    fi
    return 0
}

# Function to check if working directory is clean
check_working_directory() {
    if [ -n "$(git status --porcelain)" ]; then
        print_error "Working directory is not clean. Please commit or stash your changes."
        git status --short
        exit 1
    fi
}

# Function to create backup branch
create_backup() {
    local timestamp=$(date +%Y%m%d-%H%M%S)
    BACKUP_BRANCH="backup-before-redate-${timestamp}"
    
    print_info "Creating backup branch: $BACKUP_BRANCH"
    if [ "$DRY_RUN" = false ]; then
        git branch "$BACKUP_BRANCH"
        print_success "Backup branch created"
    else
        print_info "[DRY RUN] Would create branch: $BACKUP_BRANCH"
    fi
}

# Function to cleanup backup branch
cleanup_backup() {
    if [ "$KEEP_BACKUP" = false ] && [ -n "$BACKUP_BRANCH" ]; then
        print_info "Removing backup branch: $BACKUP_BRANCH"
        if [ "$DRY_RUN" = false ]; then
            git branch -D "$BACKUP_BRANCH" 2>/dev/null || true
            print_success "Backup branch removed"
        else
            print_info "[DRY RUN] Would remove branch: $BACKUP_BRANCH"
        fi
    else
        print_info "Keeping backup branch: $BACKUP_BRANCH"
    fi
}

# Function to restore from backup on error
restore_backup() {
    print_error "Operation failed. Restoring from backup branch..."
    if [ -n "$BACKUP_BRANCH" ] && git show-ref --verify --quiet "refs/heads/$BACKUP_BRANCH"; then
        git reset --hard "$BACKUP_BRANCH"
        print_success "Restored to backup branch"
    else
        print_error "No backup branch found. Manual recovery may be needed."
    fi
}

# Function to redate commits
redate_commits() {
    local -a commits=("${!1}")
    local -a times=("${!2}")
    
    # Find the parent of the first commit to rebase from
    local first_commit="${commits[0]}"
    local base_commit=$(git rev-parse "${first_commit}^")
    
    print_info "Base commit for rebase: $base_commit"
    print_info "Will modify ${#commits[@]} commit(s)"
    
    # Create a temporary script for the filter
    local filter_script=$(mktemp)
    cat > "$filter_script" << 'FILTER_EOF'
#!/bin/bash
# This script is called by git filter-branch for each commit

# Arrays passed via environment
IFS='|' read -ra COMMIT_HASHES <<< "$REDATE_COMMITS"
IFS='|' read -ra NEW_TIMES <<< "$REDATE_TIMES"

# Get the current commit hash
CURRENT_HASH=$(git rev-parse HEAD)

# Check if current commit is in our list
for i in "${!COMMIT_HASHES[@]}"; do
    if [[ $CURRENT_HASH == ${COMMIT_HASHES[$i]}* ]]; then
        # Extract date and time parts
        OLD_DATE="$GIT_COMMITTER_DATE"
        DATE_PART=$(echo "$OLD_DATE" | cut -d' ' -f1)
        TZ_PART=$(echo "$OLD_DATE" | awk '{print $(NF-1), $NF}')
        
        # Create new date with new time
        NEW_TIME="${NEW_TIMES[$i]}"
        NEW_DATE="$DATE_PART $NEW_TIME:00 $TZ_PART"
        
        export GIT_COMMITTER_DATE="$NEW_DATE"
        export GIT_AUTHOR_DATE="$NEW_DATE"
        echo "[REDATE] $CURRENT_HASH -> $NEW_TIME"
        break
    fi
done
FILTER_EOF
    chmod +x "$filter_script"
    
    # Prepare environment variables for the filter
    local commits_env=$(IFS='|'; echo "${commits[*]}")
    local times_env=$(IFS='|'; echo "${times[*]}")
    
    print_info "Starting rebase operation..."
    
    if [ "$DRY_RUN" = false ]; then
        # Use git filter-branch to rewrite history
        FILTER_BRANCH_SQUELCH_WARNING=1 \
        REDATE_COMMITS="$commits_env" \
        REDATE_TIMES="$times_env" \
        git filter-branch -f \
            --env-filter "source '$filter_script'" \
            "${base_commit}..HEAD" 2>&1 | while read line; do
                if [[ $line == *"[REDATE]"* ]]; then
                    print_success "$line"
                fi
            done
        
        # Cleanup filter-branch backup
        rm -rf .git/refs/original/
        
        print_success "Commits redated successfully"
    else
        print_info "[DRY RUN] Would redate commits from $base_commit to HEAD"
        for i in "${!commits[@]}"; do
            print_info "[DRY RUN]   ${commits[$i]} -> ${times[$i]}"
        done
    fi
    
    # Cleanup temp script
    rm -f "$filter_script"
}

# Function to show before/after comparison
show_comparison() {
    local -a commits=("${!1}")
    
    print_info "Commit date comparison:"
    echo ""
    
    if [ "$DRY_RUN" = false ]; then
        for commit in "${commits[@]}"; do
            # Get the commit details
            local new_info=$(git log --format="%h %ai %s" -n 1 "$commit" 2>/dev/null || echo "Not found")
            echo -e "${GREEN}AFTER:${NC}  $new_info"
        done
    else
        print_info "[DRY RUN] Comparison would be shown here"
    fi
    echo ""
}

# Function to push to remote
push_to_remote() {
    local remote="origin"
    local branch="$ORIGINAL_BRANCH"
    
    print_warning "This will FORCE PUSH to $remote/$branch and rewrite history!"
    
    if [ "$AUTO_PUSH" = false ]; then
        read -p "Do you want to push to remote? (yes/no): " confirm
        if [ "$confirm" != "yes" ]; then
            print_info "Skipping remote push. You can manually push later with:"
            print_info "  git push --force-with-lease $remote $branch"
            return
        fi
    fi
    
    if [ "$DRY_RUN" = false ]; then
        print_info "Pushing to $remote/$branch..."
        if git push --force-with-lease "$remote" "$branch"; then
            print_success "Successfully pushed to remote"
        else
            print_error "Push failed! You may need to manually resolve this."
            print_info "The local changes are still applied."
            print_info "Backup branch is available: $BACKUP_BRANCH"
            return 1
        fi
    else
        print_info "[DRY RUN] Would push to $remote/$branch with --force-with-lease"
    fi
}

# Parse command line arguments
COMMITS=""
TIMES=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --commits)
            COMMITS="$2"
            shift 2
            ;;
        --times)
            TIMES="$2"
            shift 2
            ;;
        --auto-push)
            AUTO_PUSH=true
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        --keep-backup)
            KEEP_BACKUP=true
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            ;;
    esac
done

# Validate required arguments
if [ -z "$COMMITS" ] || [ -z "$TIMES" ]; then
    print_error "Missing required arguments"
    usage
fi

# Parse commits and times into arrays
IFS=',' read -ra COMMIT_ARRAY <<< "$COMMITS"
IFS=',' read -ra TIME_ARRAY <<< "$TIMES"

# Validate array lengths match
if [ ${#COMMIT_ARRAY[@]} -ne ${#TIME_ARRAY[@]} ]; then
    print_error "Number of commits (${#COMMIT_ARRAY[@]}) must match number of times (${#TIME_ARRAY[@]})"
    exit 1
fi

# Validate time formats
for time in "${TIME_ARRAY[@]}"; do
    validate_time "$time" || exit 1
done

# Main execution
print_info "========================================="
print_info "Git Commit Redate Tool"
print_info "========================================="
print_info "Commits to modify: ${#COMMIT_ARRAY[@]}"
print_info "Mode: $([ "$DRY_RUN" = true ] && echo "DRY RUN" || echo "LIVE")"
print_info "Auto-push: $([ "$AUTO_PUSH" = true ] && echo "YES" || echo "NO")"
print_info "Keep backup: $([ "$KEEP_BACKUP" = true ] && echo "YES" || echo "NO")"
print_info "========================================="
echo ""

# Store original branch
ORIGINAL_BRANCH=$(git rev-parse --abbrev-ref HEAD)
print_info "Current branch: $ORIGINAL_BRANCH"

# Verify git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    print_error "Not a git repository"
    exit 1
fi

# Check working directory
check_working_directory

# Verify all commits exist
print_info "Verifying commits exist..."
for commit in "${COMMIT_ARRAY[@]}"; do
    if ! git rev-parse --verify "$commit^{commit}" >/dev/null 2>&1; then
        print_error "Commit not found: $commit"
        exit 1
    fi
    # Expand to full hash
    full_hash=$(git rev-parse "$commit")
    print_success "  $commit -> $full_hash"
done
echo ""

# Show before state
print_info "Current commit dates (BEFORE):"
for commit in "${COMMIT_ARRAY[@]}"; do
    info=$(git log --format="%h %ai %s" -n 1 "$commit")
    echo -e "${YELLOW}BEFORE:${NC} $info"
done
echo ""

# Create backup
create_backup

# Set trap to restore on error
trap 'restore_backup; exit 1' ERR

# Perform redate
redate_commits COMMIT_ARRAY[@] TIME_ARRAY[@]

# Show after state
show_comparison COMMIT_ARRAY[@]

# Push to remote if requested
if [ "$DRY_RUN" = false ]; then
    push_to_remote
fi

# Cleanup backup
cleanup_backup

# Remove trap
trap - ERR

print_success "========================================="
print_success "Operation completed successfully!"
print_success "========================================="

if [ "$DRY_RUN" = true ]; then
    print_info "This was a dry run. No changes were made."
    print_info "Run without --dry-run to apply changes."
fi

exit 0
