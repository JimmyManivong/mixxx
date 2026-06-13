# Guide de test - Mixxx Custom Rekordbox Style

## 🧪 Tests à effectuer après compilation

Ce guide vous permet de vérifier que toutes les modifications custom fonctionnent correctement.

---

## Prérequis

✅ Mixxx compilé et installé  
✅ Au moins 1 track audio chargé dans la bibliothèque  
✅ (Optionnel) Pioneer DDJ-FLX6 connecté  

---

## Test 1 : Waveform fixe (pré-EQ) ⭐ PRIORITÉ HAUTE

**Objectif** : Vérifier que la waveform ne bouge plus quand on ajuste les EQ.

### Procédure

1. Lancer Mixxx
2. Charger un track sur le Deck 1
3. Lancer la lecture (Play)
4. Observer la waveform
5. **Toucher aux knobs EQ** :
   - Baisser le **Low** (Basses) à fond
   - Monter le **Mid** (Médiums) à fond
   - Baisser le **High** (Aigus) à fond

### ✅ Résultat attendu

- ❌ **Mixxx standard** : La waveform bouge, rétrécit, s'agrandit
- ✅ **Mixxx custom** : **La waveform NE BOUGE PAS** du tout

**Si la waveform bouge** → La modification n'a pas fonctionné ❌

---

## Test 2 : Downbeats rouges épais ⭐ PRIORITÉ HAUTE

**Objectif** : Vérifier que des barres rouges épaisses apparaissent tous les 4 temps.

### Procédure

1. Charger un track avec un **BPM stable** (éviter les tracks variables)
2. Activer la **waveform RGB** ou **waveform filtered** (menu View → Waveform)
3. Observer la waveform

### ✅ Résultat attendu

- **Barres fines blanches** : temps 2, 3, 4 de chaque mesure
- **Barres ÉPAISSES ROUGES** : tous les 4 temps (temps 1 de chaque mesure)

**Visuellement** :
```
|      |  |  |  |      |  |  |  |      |  |  |
rouge  blanc  blanc  rouge  blanc  blanc  rouge
```

### Notes

- Si le track est en **3/4** ou autre signature, les barres rouges seront décalées (limitation connue)
- La détection suppose une signature **4/4** (la plus commune en musique électronique)

**Si toutes les barres sont blanches** → La modification n'a pas fonctionné ❌

---

## Test 3 : Couleurs 3Band Rekordbox

**Objectif** : Vérifier que les couleurs de fréquences sont celles de Rekordbox.

### Procédure

1. Charger un track avec **beaucoup de dynamique** (bass drops, vocals, cymbales)
2. Activer la **waveform RGB** (menu View → Waveform → RGB)
3. Observer les couleurs

### ✅ Résultat attendu

| Fréquence | Couleur attendue | Code couleur |
|-----------|------------------|--------------|
| **Basses (Low)** | Bleu | `#0088FF` |
| **Médiums (Mid)** | Jaune/Vert | `#88FF00` |
| **Aigus (High)** | Orange/Rouge | `#FF5500` |

**Visuellement** :
- Les **kick drums** (basses) doivent être **bleus**
- Les **voix/instruments** (mid) doivent être **jaune-vert**
- Les **cymbales/hi-hats** (high) doivent être **orange-rouge**

**Si les couleurs sont différentes** → Vérifier que le skin LateNight est actif

---

## Test 4 : BeatHighlightColor XML fonctionnel

**Objectif** : Vérifier que le tag XML `<BeatHighlightColor>` est bien lu.

### Procédure

1. Ouvrir le fichier `res/skins/LateNight/waveform.xml`
2. Vérifier la ligne 32 :
   ```xml
   <BeatHighlightColor>#FF0000</BeatHighlightColor>
   ```
3. **Modifier** la couleur, par exemple :
   ```xml
   <BeatHighlightColor>#00FF00</BeatHighlightColor><!-- Vert -->
   ```
4. Relancer Mixxx
5. Observer les downbeats

### ✅ Résultat attendu

- Les downbeats doivent maintenant être **VERTS** au lieu de rouges

**Remettre** ensuite à `#FF0000` (rouge) pour le style Rekordbox.

---

## Test 5 : Performance sur Raspberry Pi 4

**Objectif** : Vérifier que Mixxx est fluide sur Raspberry Pi 4 (2GB RAM).

### Procédure

1. Charger 2 tracks (Deck 1 et Deck 2)
2. Lancer les deux en lecture simultanée
3. Effectuer des **scratch** avec les jog wheels (si DDJ-FLX6 connecté)
4. Activer des **effets** (Delay, Reverb, etc.)
5. Observer la **latence audio** et le **framerate visuel**

### ✅ Résultat attendu

- ✅ Pas de crackling audio
- ✅ Waveform fluide (pas de lag)
- ✅ CPU usage < 80% (visible dans `htop`)

### Si problèmes de performance

**Solutions** :
1. Baisser la **résolution de waveform** : Préférences → Waveform → Quality
2. Désactiver les **effets visuels** : Préférences → Interface → Disable animations
3. Utiliser **LXDE** au lieu de GNOME/KDE (desktop léger)

---

## Test 6 : Pioneer DDJ-FLX6 (optionnel)

**Objectif** : Vérifier que le contrôleur est bien détecté et mappé.

### Procédure

1. Connecter le DDJ-FLX6 en USB
2. Lancer Mixxx
3. Vérifier : **Options → Préférences → Controllers**
   - Le DDJ-FLX6 doit apparaître dans la liste
   - Mapping : **fixxiefixx** (auto-détecté)
4. Tester :
   - Jog wheels (scratch)
   - Pads (hotcues)
   - Faders (volume, crossfader)
   - EQ knobs
   - FX buttons

### ✅ Résultat attendu

Tous les contrôles fonctionnent comme prévu.

**Si le contrôleur n'est pas détecté** :
```bash
# Vérifier que le périphérique USB est visible
lsusb | grep Pioneer
```

---

## Checklist complète ✅

Cochez au fur et à mesure :

- [ ] **Test 1** : Waveform fixe (ne bouge pas avec les EQ)
- [ ] **Test 2** : Downbeats rouges épais tous les 4 temps
- [ ] **Test 3** : Couleurs 3Band (Bleu/Jaune-Vert/Orange-Rouge)
- [ ] **Test 4** : BeatHighlightColor modifiable en XML
- [ ] **Test 5** : Performance fluide sur Raspberry Pi 4
- [ ] **Test 6** : DDJ-FLX6 détecté et fonctionnel (optionnel)

---

## 🐛 Rapporter un bug

Si un test échoue :

1. Noter exactement quel test a échoué
2. Prendre une **capture d'écran** de la waveform
3. Vérifier les **logs** : `~/.mixxx/mixxx.log`
4. Contacter : jimmy.manivong1@gmail.com

---

## 🎉 Tous les tests passent ?

**Félicitations !** Votre Mixxx custom est prêt à mixer 🎛️🔊

**Profitez bien de votre station DJ Raspberry Pi 4 + DDJ-FLX6 !**
