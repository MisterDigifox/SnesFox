#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

/**
 * Dump complet de la ROM en assembleur.
 *
 * - Désassemblage récursif (code réel)
 * - Génération automatique de labels
 * - Zones non exécutées exportées en .db
 *
 * @param rom           Données de la ROM (sans header copier)
 * @param resetVector   Adresse de départ (PC initial)
 * @param filename      Fichier de sortie (ex: "output.asm")
 * @param coverageHits  Si non nullptr, PCs présents dans l’ensemble reçoivent un suffixe "; cov"
 *                      (couverture dynamique depuis snesfox cov).
 */
extern void dumpRomAsAsmFull(const std::vector<uint8_t>& rom,
                             uint16_t resetVector,
                             const std::string& filename,
                             const std::unordered_set<uint32_t>* coverageHits = nullptr);
