# -*- coding: utf-8 -*-
# ...existing code...
"""
簡易エンコーディング修正ツール
使い方:
  # プレビュー（変更候補を表示するだけ）
  python tools\fix_japanese_encoding.py --root . --check

  # 実行（バックアップを作って上書き）
  python tools\fix_japanese_encoding.py --root . --apply --backup

必要なら pip install chardet
"""
import argparse
import os
import sys
import shutil
import difflib

try:
    import chardet
except Exception:
    chardet = None

CAND_EXT = {'.c', '.cpp', '.h', '.hpp', '.ino', '.py', '.md', '.txt', '.json', '.yaml', '.yml'}

def detect_encoding(b):
    if chardet:
        res = chardet.detect(b)
        enc = res.get('encoding')
        if enc:
            return enc
    # fallback tries
    for enc in ('utf-8', 'cp932', 'euc_jp', 'iso2022_jp'):
        try:
            b.decode(enc)
            return enc
        except Exception:
            pass
    return None

def contains_japanese(s):
    for ch in s:
        o = ord(ch)
        if (0x3040 <= o <= 0x30FF) or (0x4E00 <= o <= 0x9FFF) or (0x3000 <= o <= 0x303F):
            return True
    return False

def process_file(path, apply=False, backup=False):
    with open(path, 'rb') as f:
        b = f.read()
    # skip small ASCII-only files
    if all(c < 0x80 for c in b):
        return None
    enc = detect_encoding(b)
    if not enc:
        return ('unknown', None, None)
    try:
        decoded = b.decode(enc)
    except Exception:
        return ('fail-to-decode', enc, None)
    # if already utf-8 and contains japanese, consider OK
    if enc.lower() in ('utf-8', 'utf_8') and contains_japanese(decoded):
        return ('ok-utf8', enc, None)
    # if decoded text contains japanese chars, suggest converting to utf-8
    if contains_japanese(decoded):
        new_bytes = decoded.encode('utf-8')
        if apply:
            if backup:
                shutil.copy2(path, path + '.bak')
            with open(path, 'wb') as f:
                f.write(new_bytes)
        # prepare preview diff
        old_text = b.decode(enc, errors='replace')
        diff = '\n'.join(list(difflib.unified_diff(
            old_text.splitlines(), decoded.splitlines(),
            fromfile=f'{path} (decoded as {enc})', tofile=f'{path} (re-encoded utf-8)', lineterm='')))
        return ('convert', enc, diff)
    return ('no-japanese', enc, None)

def iter_files(root, exts):
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if exts and os.path.splitext(fn)[1].lower() not in exts:
                continue
            yield os.path.join(dirpath, fn)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--root', default='.', help='workspace root')
    p.add_argument('--check', action='store_true', help='preview only (default)')
    p.add_argument('--apply', action='store_true', help='apply changes')
    p.add_argument('--backup', action='store_true', help='create .bak backups when applying')
    p.add_argument('--exts', default=','.join(sorted(CAND_EXT)), help='comma separated extensions to check')
    args = p.parse_args()
    exts = set('.' + e.lstrip('.').lower() for e in args.exts.split(',') if e)
    if not args.apply:
        args.check = True
    root = os.path.abspath(args.root)
    any_issue = False
    for path in iter_files(root, exts):
        res = process_file(path, apply=args.apply, backup=args.backup)
        if not res:
            continue
        status, enc, diff = res
        if status in ('convert', 'fail-to-decode', 'unknown'):
            any_issue = True
            print(f'[{status}] {path} (detected: {enc})')
            if diff and args.check:
                print('--- preview diff ---')
                print(diff[:2000])
                if len(diff) > 2000:
                    print('...diff trimmed...')
            if args.apply:
                print(' -> converted to UTF-8 (backup created)' if args.backup else ' -> converted to UTF-8')
        elif status == 'ok-utf8':
            # fine
            pass
    if not any_issue:
        print('問題ありそうな日本語ファイルは見つかりませんでした。')

if __name__ == '__main__':
    main()