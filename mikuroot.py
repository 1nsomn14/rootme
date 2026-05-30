import subprocess
import os
import sys


def jalankan_manajer_proses():
    folder_plugins = "plugins"
    print("[*] Memulai interaksi otomatisasi manajer proses...")

    if not os.path.exists(folder_plugins) or not os.path.isdir(folder_plugins):
        print(f"[-] Error: Folder '{folder_plugins}' tidak ditemukan!")
        print("[*] Silakan buat folder tersebut dan isi dengan biner uji coba lu.")
        sys.exit(1)

    daftar_biner = [
        f for f in os.listdir(folder_plugins)
        if os.path.isfile(os.path.join(folder_plugins, f))
    ]
    daftar_biner.sort()  

    if not daftar_biner:
        print("[-] Folder plugins kosong. Tidak ada biner yang bisa dieksekusi.")
        sys.exit(0)

    print(f"[+] Ditemukan {len(daftar_biner)} biner siap uji di dalam folder '{folder_plugins}'.")
    print("-" * 60)

    for nama_file in daftar_biner:
        jalur_penuh = os.path.join(folder_plugins, nama_file)
        print(f"\n[*] Menjadwalkan eksekusi berkas: {nama_file}")

        if not os.access(jalur_penuh, os.X_OK):
            print(f"[!] Warning: '{nama_file}' tidak memiliki izin eksekusi. Mencoba memberikan izin...")
            try:
                os.chmod(jalur_penuh, 0o755)
            except Exception as e:
                print(f"[-] Gagal mengubah izin file: {e}")
                continue

        print(f"[->] Menjalankan: {jalur_penuh} ...")
        try:
            hasil = subprocess.run([jalur_penuh], check=False)
            print(f"[*] {nama_file} selesai berjalan dengan Exit Code: {hasil.returncode}")

            if hasil.returncode == 0:
                print(f"[+] [SUKSES] Kondisi terpenuhi pada biner: {nama_file}!")
                print("[*] Alur otomatisasi dihentikan secara aman karena target berhasil.")
                break
            else:
                print(f"[-] [GAGAL] {nama_file} tidak memenuhi kondisi sistem. Melanjutkan ke biner berikutnya...")

        except KeyboardInterrupt:
            print("\n[!] Eksekusi dihentikan paksa oleh pengguna (Ctrl+C).")
            sys.exit(0)
        except Exception as e:
            print(f"[-] Terjadi kesalahan runtime saat mengeksekusi {nama_file}: {e}")
            continue

    print("\n[*] Seluruh rangkaian tugas automasi manajer proses selesai.")


if __name__ == "__main__":
    jalankan_manajer_proses()