// SPDX-License-Identifier: GPL-3.0-only
// ObsSynth FM-1: 2026 Orian Beltrame
// The page's language: index.html is English, pt.html Portuguese (their <html lang>); tr(en, pt) picks the string.
export const LANG = (globalThis.document?.documentElement?.lang || 'en').toLowerCase().startsWith('pt') ? 'pt' : 'en';
export const tr = (en, pt) => (LANG === 'pt' ? pt : en);
export const LOCALE = LANG === 'pt' ? 'pt-BR' : 'en-US';
