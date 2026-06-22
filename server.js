const express = require('express');
const cors = require('cors');
const sqlite3 = require('sqlite3').verbose();
const app = express();

app.use(cors());
app.use(express.json());
app.use(express.static('public'));

// === 1. KONEKSI DATABASE ===
const db = new sqlite3.Database('./iot_data.db', (err) => {
    if (!err) {
        console.log("Database SQLite SMART FARM Siap.");
        db.run(`CREATE TABLE IF NOT EXISTS log_iot (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            mode_sistem TEXT,
            mode_fan TEXT,
            nh3 REAL,
            kelembapan REAL,
            suhu REAL,
            pemicu TEXT
        )`);
    }
});

// State Global Terkini (Untuk monitoring widget atas)
let currentState = {
    mode_sistem: "AUTO",
    mode_fan: "OFF",
    nh3: 0.0,
    kelembapan: 0.0,
    suhu: 0.0
};

// Variabel bantu untuk melacak interval 5 menit di sisi server
let lastLogTime = 0; 

// === 2. API ENDPOINTS ===

// Jalur widget atas (Bebas Akses)
app.get('/api/status', (req, res) => {
    res.json(currentState);
});

// Jalur kirim data ESP32 (Bebas Akses)
app.post('/api/update', (req, res) => {
    const { temp, hum, nh3, mode, fan, pemicu } = req.body;
    const now = Date.now();

    // -------------------------------------------------------------------------
    // Skenario 1: SELALU UPDATE STATE GLOBAL
    // -------------------------------------------------------------------------
    if (temp !== undefined) currentState.suhu = parseFloat(temp);
    if (hum !== undefined) currentState.kelembapan = parseFloat(hum);
    if (nh3 !== undefined) currentState.nh3 = parseFloat(nh3);

    const modeBerubah = (mode && mode !== currentState.mode_sistem);
    const fanBerubah = (fan && fan !== currentState.mode_fan);
    const sudah5Menit = (now - lastLogTime >= 5 * 60 * 1000 || lastLogTime === 0);

    if (mode !== undefined) currentState.mode_sistem = mode;
    if (fan !== undefined) currentState.mode_fan = fan;

    // -------------------------------------------------------------------------
    // Skenario 2: KONTROL PENYIMPANAN LOG DATABASE
    // -------------------------------------------------------------------------
    let butuhSimpan = modeBerubah || fanBerubah || sudah5Menit || pemicu === "Rutin 5 Menit";

    if (butuhSimpan) {
        let labelPemicu = pemicu || "Update Otomatis";
        if (modeBerubah) labelPemicu = "Perubahan Mode";
        if (fanBerubah) labelPemicu = "Perubahan Status Fan";
        if (sudah5Menit && !modeBerubah && !fanBerubah) labelPemicu = "Rutin 5 Menit";

        const stmt = db.prepare(`INSERT INTO log_iot (mode_sistem, mode_fan, nh3, kelembapan, suhu, pemicu) VALUES (?, ?, ?, ?, ?, ?)`);
        stmt.run(currentState.mode_sistem, currentState.mode_fan, currentState.nh3, currentState.kelembapan, currentState.suhu, labelPemicu);
        stmt.finalize();

        if (sudah5Menit) {
            lastLogTime = now;
        }

        console.log(`[DATABASE LOG] Data disimpan! -> T: ${currentState.suhu}°C | NH3: ${currentState.nh3} PPM | Pemicu: ${labelPemicu}`);

        db.run(`DELETE FROM log_iot WHERE timestamp < datetime('now', '-3 days')`);
    }

    res.json({ status: "Success", current: currentState });
});

// Jalur grafik & tabel bawah (Bebas Akses)
app.get('/api/history', (req, res) => {
    const query = `
        SELECT *, 
        date(timestamp, 'localtime') as tanggal_lokal, 
        time(timestamp, 'localtime') as jam_lokal,
        CASE 
            WHEN timestamp >= datetime('now', '-1 day') THEN 'day1'
            WHEN timestamp >= datetime('now', '-2 days') AND timestamp < datetime('now', '-1 day') THEN 'day2'
            ELSE 'day3'
        END as kelompok_hari
        FROM log_iot 
        ORDER BY timestamp DESC
    `;
    
    db.all(query, [], (err, rows) => {
        if (err) return res.status(500).json({ error: err.message });
        res.json(rows);
    });
});

const PORT = process.env.PORT || 5000;
app.listen(PORT, '0.0.0.0', () => console.log(`Server Smart Farm berjalan di port ${PORT}`));
