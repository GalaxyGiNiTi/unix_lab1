// dedup_hardlinks.cpp
#include <iostream>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <cstdint>
#include <sys/stat.h>  // для stat() — проверка устройства и inode

namespace fs = std::filesystem;

// Простой 64-битный FNV-1a хэш всего содержимого файла
std::uint64_t hash_file(const fs::path &p, std::error_code &ec) {
    ec.clear();
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        ec = std::make_error_code(std::errc::io_error);
        return 0;
    }

    const std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
    const std::uint64_t FNV_PRIME        = 1099511628211ull;

    std::uint64_t hash = FNV_OFFSET_BASIS;

    char buffer[8192];
    while (in) {
        in.read(buffer, sizeof(buffer));
        std::streamsize bytes_read = in.gcount();
        for (std::streamsize i = 0; i < bytes_read; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= FNV_PRIME;
        }
    }

    if (!in.eof()) {
        // какая-то ошибка чтения
        ec = std::make_error_code(std::errc::io_error);
    }

    return hash;
}

int main(int argc, char* argv[]) {
    // Корневой каталог: либо передан как аргумент, либо текущий
    fs::path root;
    if (argc > 1) {
        root = fs::path(argv[1]);
    } else {
        root = fs::current_path();
    }

    std::error_code ec;

    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        std::cerr << "Ошибка: путь \"" << root.string()
                  << "\" не существует или не является каталогом\n";
        return 1;
    }

    // Хэш -> путь к «оригинальному» файлу
    std::unordered_map<std::uint64_t, fs::path> hash_to_path;

    std::cout << "Обход каталога: " << root.string() << "\n";

    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            std::cerr << "Ошибка обхода: " << ec.message() << "\n";
            continue;
        }

        const fs::directory_entry &entry = *it;

        // Нас интересуют только обычные файлы (без каталогов, симлинков и т.п.)
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }

        fs::path file_path = entry.path();

        // Считаем хэш
        std::uint64_t h = hash_file(file_path, ec);
        if (ec) {
            std::cerr << "Не удалось посчитать хэш для файла "
                      << file_path.string() << ": " << ec.message() << "\n";
            ec.clear();
            continue;
        }

        auto found = hash_to_path.find(h);
        if (found == hash_to_path.end()) {
            // Встречаем этот хэш впервые — запоминаем файл как «канонический»
            hash_to_path[h] = file_path;
            std::cout << "[UNIQUE] " << file_path.string() << "\n";
        } else {
            fs::path canonical_path = found->second;

            // Используем stat() для получения информации о файлах (устройство и inode)
            struct stat stat1, stat2;
            if (stat(canonical_path.c_str(), &stat1) != 0 ||
                stat(file_path.c_str(), &stat2) != 0) {
                std::cerr << "Не удалось получить информацию о файле: "
                          << file_path.string() << "\n";
                continue;
            }

            // Не пытаемся делать жёсткие ссылки между разными файловыми системами
            if (stat1.st_dev != stat2.st_dev) {
                std::cerr << "Нельзя создать жёсткую ссылку (разные устройства) для "
                          << file_path.string() << "\n";
                continue;
            }

            // Проверяем, не являются ли файлы уже жёсткими ссылками друг на друга
            if (stat1.st_ino == stat2.st_ino) {
                std::cout << "[ALREADY LINKED] " << file_path.string()
                          << " -> " << canonical_path.string() << "\n";
                continue;
            }

            // Доп. проверка: совпадает ли размер (минимальная защита от коллизий)
            if (stat1.st_size != stat2.st_size) {
                // Очень редкая ситуация: хэш совпал, размер нет — считаем это коллизией
                std::cerr << "Возможная коллизия хэша: "
                          << file_path.string() << " и "
                          << canonical_path.string() << "\n";
                // Для простоты считаем этот файл отдельным
                hash_to_path[h] = file_path;
                continue;
            }

            // Здесь считаем, что файлы действительно одинаковые.
            // Удаляем текущий файл и создаём на его месте жёсткую ссылку.
            std::cout << "[DUPLICATE] " << file_path.string()
                      << " -> " << canonical_path.string() << "\n";

            fs::remove(file_path, ec);
            if (ec) {
                std::cerr << "Не удалось удалить файл "
                          << file_path.string() << ": " << ec.message() << "\n";
                ec.clear();
                continue;
            }

            fs::create_hard_link(canonical_path, file_path, ec);
            if (ec) {
                std::cerr << "Не удалось создать жёсткую ссылку вместо "
                          << file_path.string() << ": " << ec.message() << "\n";
                ec.clear();
                continue;
            }
        }
    }

    std::cout << "Готово.\n";
    return 0;
}
