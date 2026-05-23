set -euo pipefail

D="$(cd "$(dirname "$0")" && pwd)"
IMG="$D/ext2.img"
OUT="$D/extracted"

PASS=0; FAIL=0
ok()   { echo "  [PASS] $*"; PASS=$((PASS+1)); }
fail() { echo "  [FAIL] $*"; FAIL=$((FAIL+1)); }

inode_of() { debugfs -R "stat $1" "$IMG" 2>/dev/null | awk '/^Inode:/{print $2}'; }

echo "=== 1. Создание образа ==="

if [ ! -f "$IMG" ]; then
    truncate --size=256M "$IMG"
    mkfs.ext2 -b 2048 -N 512 "$IMG" >/dev/null 2>&1
    echo "  Создан $IMG (256M, блок 2K, 512 инод)"
else
    echo "  Используется существующий $IMG"
fi

echo "=== 2. Заполнение образа ==="

TMP=$(mktemp -d -p /tmp)
trap 'rm -rf "$TMP"' EXIT

printf 'Hello, ext2!\n' > "$TMP/small.txt"

# medium.bin — 40 KiB при блоке 2K = 20 блоков > 12 -> нужна косвенная адресация
dd if=/dev/urandom bs=1024 count=40 of="$TMP/medium.bin" 2>/dev/null

# sparse.bin — разреженный файл >4G, преимущественно пустой
truncate --size=5G "$TMP/sparse.bin"
printf 'SPARSE_BEGIN' | dd of="$TMP/sparse.bin" bs=1 seek=0            conv=notrunc 2>/dev/null
printf 'SPARSE__END_' | dd of="$TMP/sparse.bin" bs=1 seek=$((5*1024*1024*1024-12)) conv=notrunc 2>/dev/null

# файлы в подкаталогах
printf 'file in subdir\n' > "$TMP/sub.txt"
printf 'nested\n'         > "$TMP/nested.txt"

debugfs -w "$IMG" <<EOF >/dev/null 2>&1
mkdir /dira
mkdir /dirb
mkdir /dirb/sub
write $TMP/small.txt /small.txt
write $TMP/medium.bin /medium.bin
write $TMP/sparse.bin /sparse.bin
write $TMP/sub.txt /dira/sub.txt
write $TMP/nested.txt /dirb/sub/nested.txt
EOF
echo "  Образ заполнен"

INO_ROOT=$(inode_of /)
INO_DIRA=$(inode_of /dira)
INO_DIRB=$(inode_of /dirb)
INO_SUB=$(inode_of /dirb/sub)
INO_SMALL=$(inode_of /small.txt)
INO_MEDIUM=$(inode_of /medium.bin)
INO_SPARSE=$(inode_of /sparse.bin)
INO_SUBTXT=$(inode_of /dira/sub.txt)
INO_NESTED=$(inode_of /dirb/sub/nested.txt)

echo "  Иноды: / $INO_ROOT  /dira $INO_DIRA  /dirb $INO_DIRB  /dirb/sub $INO_SUB"
echo "         small=$INO_SMALL medium=$INO_MEDIUM sparse=$INO_SPARSE"
echo "         sub.txt=$INO_SUBTXT nested.txt=$INO_NESTED"

mkdir -p "$OUT"
"$D/ext2cat" "$IMG" "$INO_SMALL" > "$OUT/small.txt"
"$D/ext2cat" "$IMG" "$INO_MEDIUM" > "$OUT/medium.bin"
"$D/ext2cat" "$IMG" "$INO_SUBTXT" > "$OUT/sub.txt"
"$D/ext2cat" "$IMG" "$INO_NESTED" > "$OUT/nested.txt"

sha512sum "$OUT/small.txt" "$OUT/medium.bin" "$OUT/sub.txt" "$OUT/nested.txt" \
    > "$OUT/checksums.sha512"
echo "  Контрольные суммы сохранены в $OUT/checksums.sha512"

# -----------------------------------------
echo "=== 3. ext2info ==="

if "$D/ext2info" "$IMG" "$INO_SMALL" 2>/dev/null | grep -q "regular file"; then
    ok "small.txt — regular file"
else
    fail "small.txt — regular file"
fi

if "$D/ext2info" "$IMG" "$INO_DIRA" 2>/dev/null | grep -qi "directory"; then
    ok "/dira — directory"
else
    fail "/dira — directory"
fi

# Косвенная адресация (medium.bin: 20 блоков > 12 прямых)
if "$D/ext2info" "$IMG" "$INO_MEDIUM" 2>/dev/null | grep -qi "indirect"; then
    ok "medium.bin — отображаются косвенные блоки"
else
    fail "medium.bin — отображаются косвенные блоки"
fi

# Размер разреженного файла >4G (5368709120 = 5*1024^3)
if "$D/ext2info" "$IMG" "$INO_SPARSE" 2>/dev/null | grep -q "5368709120"; then
    ok "sparse.bin — размер 5 GiB корректен"
else
    fail "sparse.bin — размер 5 GiB корректен"
fi

# Дыры в карте блоков разреженного файла
if "$D/ext2info" "$IMG" "$INO_SPARSE" 2>/dev/null | grep -q "hole"; then
    ok "sparse.bin — карта блоков содержит дыры"
else
    fail "sparse.bin — карта блоков содержит дыры"
fi

# -----------------------------------------
echo "=== 4. ext2cat ==="

# Сверяем sha512 через пайп: yourgetinodedata ext2.img inode | sha512sum
while IFS='  ' read -r want path; do
    fname="${path##*/}"
    ino=$(inode_of "/${path#$OUT/}" 2>/dev/null) || true
    case "$fname" in
        small.txt)  ino=$INO_SMALL  ;;
        medium.bin) ino=$INO_MEDIUM ;;
        sub.txt)    ino=$INO_SUBTXT ;;
        nested.txt) ino=$INO_NESTED ;;
        *) continue ;;
    esac
    got=$("$D/ext2cat" "$IMG" "$ino" 2>/dev/null | sha512sum | awk '{print $1}')
    if [ "$got" = "$want" ]; then
        ok "sha512 совпадает: $fname"
    else
        fail "sha512 не совпадает: $fname"
    fi
done < "$OUT/checksums.sha512"

# Размер разреженного файла: ext2cat должен вернуть ровно 5 GiB нулей+данных
sparse_bytes=$("$D/ext2cat" "$IMG" "$INO_SPARSE" 2>/dev/null | wc -c)
if [ "$sparse_bytes" -eq $((5*1024*1024*1024)) ]; then
    ok "sparse.bin — ext2cat вернул 5 GiB"
else
    fail "sparse.bin — ожидали $((5*1024*1024*1024)), получили $sparse_bytes"
fi

# -----------------------------------------
echo "=== 5. ext2ls ==="

# Проверяем имена и inode через пайп ext2cat | ext2ls
check_dir() {
    local dir_ino="$1" name="$2" exp_ino="$3"
    local listing
    listing=$("$D/ext2cat" "$IMG" "$dir_ino" 2>/dev/null | "$D/ext2ls" 2>/dev/null)
    if ! echo "$listing" | grep -qF "$name"; then
        fail "в листинге inode=$dir_ino не найдено '$name'"
        return
    fi
    local got_ino
    got_ino=$(echo "$listing" | awk -v n="$name" '$3==n{print $1}')
    if [ "$got_ino" = "$exp_ino" ]; then
        ok "inode=$dir_ino содержит $name (inode $exp_ino)"
    else
        fail "inode=$dir_ino: $name — ожидали inode $exp_ino, получили '$got_ino'"
    fi
}

check_dir "$INO_ROOT"  "dira"       "$INO_DIRA"
check_dir "$INO_ROOT"  "dirb"       "$INO_DIRB"
check_dir "$INO_ROOT"  "small.txt"  "$INO_SMALL"
check_dir "$INO_DIRA"  "sub.txt"    "$INO_SUBTXT"
check_dir "$INO_DIRB"  "sub"        "$INO_SUB"
check_dir "$INO_SUB"   "nested.txt" "$INO_NESTED"

# --------------------------------------------
echo "=== 6. loop-устройство ==="

if ! command -v losetup >/dev/null 2>&1; then
    echo "  losetup не найден, пропускаем"
elif LOOP=$(sudo losetup -f 2>/dev/null); then
    sudo losetup "$LOOP" "$IMG"
    echo "  $IMG → $LOOP  ($(lsblk -no size,fstype "$LOOP" 2>/dev/null || true))"

    # ext2info, ext2cat, ext2ls — те же тесты, но устройство вместо файла
    if "$D/ext2info" "$LOOP" "$INO_MEDIUM" 2>/dev/null | grep -qi "indirect"; then
        ok "loop: ext2info medium.bin — косвенные блоки"
    else
        fail "loop: ext2info medium.bin — косвенные блоки"
    fi

    while IFS='  ' read -r want path; do
        fname="${path##*/}"
        case "$fname" in
            small.txt)  ino=$INO_SMALL  ;;
            medium.bin) ino=$INO_MEDIUM ;;
            sub.txt)    ino=$INO_SUBTXT ;;
            nested.txt) ino=$INO_NESTED ;;
            *) continue ;;
        esac
        got=$("$D/ext2cat" "$LOOP" "$ino" 2>/dev/null | sha512sum | awk '{print $1}')
        if [ "$got" = "$want" ]; then
            ok "loop sha512: $fname"
        else
            fail "loop sha512: $fname"
        fi
    done < "$OUT/checksums.sha512"

    root_ls=$("$D/ext2cat" "$LOOP" "$INO_ROOT" 2>/dev/null | "$D/ext2ls" 2>/dev/null)
    if echo "$root_ls" | grep -qF "dira"; then
        ok "loop: ext2ls root содержит dira"
    else
        fail "loop: ext2ls root содержит dira"
    fi

    sudo losetup -d "$LOOP"
    echo "  $LOOP отключён"
else
    echo "  sudo losetup недоступен, пропускаем"
fi

# --------------------------------------------
echo ""
echo "Результат: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]