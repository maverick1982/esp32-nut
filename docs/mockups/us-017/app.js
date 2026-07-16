/**
 * ESP32 NUT Server — OTA Firmware Update Mockup
 * Interactive prototype for US-017
 */

(function () {
    'use strict';

    // --- Tab Navigation ---
    const tabs = document.querySelectorAll('.tab');
    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            // This mockup only shows the OTA tab
            tabs.forEach(t => t.classList.remove('active'));
            tab.classList.add('active');
        });
    });

    // --- Dropzone ---
    const dropzone = document.getElementById('dropzone');
    const fileInput = document.getElementById('file-input');
    const fileInfo = document.getElementById('file-info');
    const fileName = document.getElementById('file-name');
    const fileSize = document.getElementById('file-size');
    const fileRemove = document.getElementById('file-remove');
    const uploadActions = document.getElementById('upload-actions');
    const btnFlash = document.getElementById('btn-flash');

    // States
    const stateIdle = document.getElementById('state-idle');
    const stateProgress = document.getElementById('state-progress');
    const stateSuccess = document.getElementById('state-success');
    const stateError = document.getElementById('state-error');

    let selectedFile = null;

    // --- OTA Type Selection ---
    const radioFirmware = document.querySelector('input[value="firmware"]');
    const radioFilesystem = document.querySelector('input[value="filesystem"]');
    const fsWarning = document.getElementById('fs-warning');
    const uploadHint = document.getElementById('upload-hint');

    function updateOtaType() {
        if (radioFilesystem.checked) {
            fsWarning.style.display = 'flex';
            uploadHint.textContent = "Seleziona littlefs.bin per aggiornare l'interfaccia web";
        } else {
            fsWarning.style.display = 'none';
            uploadHint.textContent = "Seleziona firmware.bin per aggiornare l'applicazione";
        }
    }

    radioFirmware.addEventListener('change', updateOtaType);
    radioFilesystem.addEventListener('change', updateOtaType);

    // Click to open file picker
    dropzone.addEventListener('click', (e) => {
        if (e.target.closest('.file-remove') || e.target.closest('.file-info')) return;
        fileInput.click();
    });

    // Drag and drop
    dropzone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropzone.classList.add('dragover');
    });

    dropzone.addEventListener('dragleave', () => {
        dropzone.classList.remove('dragover');
    });

    dropzone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropzone.classList.remove('dragover');
        const files = e.dataTransfer.files;
        if (files.length > 0) {
            handleFileSelect(files[0]);
        }
    });

    fileInput.addEventListener('change', () => {
        if (fileInput.files.length > 0) {
            handleFileSelect(fileInput.files[0]);
        }
    });

    function handleFileSelect(file) {
        selectedFile = file;
        fileName.textContent = file.name;
        fileSize.textContent = formatSize(file.size);

        // Show file info, hide upload prompts
        dropzone.classList.add('has-file');
        dropzone.querySelector('.upload-icon').style.display = 'none';
        dropzone.querySelector('.upload-label').style.display = 'none';
        dropzone.querySelector('.upload-sublabel').style.display = 'none';
        fileInfo.classList.add('visible');
        uploadActions.style.display = 'flex';
        btnFlash.disabled = false;
    }

    function clearFile() {
        selectedFile = null;
        fileInput.value = '';
        dropzone.classList.remove('has-file');
        dropzone.querySelector('.upload-icon').style.display = '';
        dropzone.querySelector('.upload-label').style.display = '';
        dropzone.querySelector('.upload-sublabel').style.display = '';
        fileInfo.classList.remove('visible');
        uploadActions.style.display = 'none';
        btnFlash.disabled = true;
    }

    fileRemove.addEventListener('click', (e) => {
        e.stopPropagation();
        clearFile();
    });

    // --- Flash Button: Start Upload Simulation ---
    btnFlash.addEventListener('click', () => {
        if (!selectedFile) return;
        startUploadSimulation();
    });

    // --- Build Progress Segments ---
    const SEGMENT_COUNT = 20;
    const progressTrack = document.getElementById('progress-track');
    for (let i = 0; i < SEGMENT_COUNT; i++) {
        const seg = document.createElement('div');
        seg.className = 'progress-segment';
        progressTrack.appendChild(seg);
    }
    const segments = progressTrack.querySelectorAll('.progress-segment');

    // --- Upload Simulation ---
    function startUploadSimulation() {
        // Hide idle, show progress
        stateIdle.style.display = 'none';
        stateProgress.classList.add('visible');
        stateSuccess.classList.remove('visible');
        stateError.classList.remove('visible');

        // Reset segments
        segments.forEach(s => {
            s.classList.remove('filled', 'filling');
        });

        // Reset phases
        setPhase('upload');

        const totalSize = selectedFile ? selectedFile.size : 863027;
        const totalKB = (totalSize / 1024).toFixed(1);
        let progress = 0;
        const speed = 85 + Math.random() * 40; // KB/s

        const progressPercent = document.getElementById('progress-percent');
        const progressBytes = document.getElementById('progress-bytes');
        const progressSpeed = document.getElementById('progress-speed');
        const progressStatus = document.getElementById('progress-status');

        progressStatus.textContent = 'Caricamento in corso...';

        const interval = setInterval(() => {
            progress += (1.5 + Math.random() * 1.5);
            if (progress > 100) progress = 100;

            const pct = Math.round(progress);
            progressPercent.textContent = pct + '%';
            progressBytes.textContent = ((progress / 100) * totalKB).toFixed(1) + ' KB / ' + totalKB + ' KB';
            progressSpeed.textContent = (speed + Math.random() * 20 - 10).toFixed(0) + ' KB/s';

            // Update segments
            const filledCount = Math.floor((progress / 100) * SEGMENT_COUNT);
            segments.forEach((seg, i) => {
                seg.classList.remove('filled', 'filling');
                if (i < filledCount) {
                    seg.classList.add('filled');
                } else if (i === filledCount && progress < 100) {
                    seg.classList.add('filling');
                }
            });

            if (progress >= 100) {
                clearInterval(interval);
                progressSpeed.textContent = '--';
                // Move to verification
                setTimeout(() => {
                    setPhase('verify');
                    progressStatus.textContent = 'Verifica integrità...';
                    setTimeout(() => {
                        setPhase('commit');
                        progressStatus.textContent = 'Commit partizione OTA...';
                        setTimeout(() => {
                            setPhase('reboot');
                            progressStatus.textContent = 'Preparazione riavvio...';
                            setTimeout(() => {
                                // Choose: success (90%) or error (10%) for demo
                                if (Math.random() > 0.1) {
                                    showSuccess();
                                } else {
                                    showError('Errore durante la verifica: hash non corrispondente.');
                                }
                            }, 800);
                        }, 600);
                    }, 900);
                }, 500);
            }
        }, 80);
    }

    // --- Phase Indicator ---
    const phases = ['upload', 'verify', 'commit', 'reboot'];
    function setPhase(current) {
        const currentIdx = phases.indexOf(current);
        phases.forEach((p, i) => {
            const el = document.getElementById('phase-' + p);
            el.classList.remove('active', 'completed');
            if (i < currentIdx) {
                el.classList.add('completed');
            } else if (i === currentIdx) {
                el.classList.add('active');
            }
        });
    }

    // --- Success State ---
    function showSuccess() {
        stateProgress.classList.remove('visible');
        stateSuccess.classList.add('visible');

        let count = 5;
        const countdownEl = document.getElementById('countdown');
        countdownEl.textContent = count;

        const cdInterval = setInterval(() => {
            count--;
            countdownEl.textContent = count;
            if (count <= 0) {
                clearInterval(cdInterval);
                // Simulate reboot: flash screen, then reset to idle
                document.body.style.opacity = '0';
                document.body.style.transition = 'opacity 0.6s ease';
                setTimeout(() => {
                    resetToIdle();
                    // Update version to show it worked
                    document.getElementById('fw-version').textContent = '1.3.0';
                    document.getElementById('fw-build').textContent = 'Jul 16 2026 18:15';
                    document.getElementById('fw-partition').textContent = 'app1';
                    document.body.style.opacity = '1';
                }, 800);
            }
        }, 1000);
    }

    // --- Error State ---
    function showError(msg) {
        stateProgress.classList.remove('visible');
        stateError.classList.add('visible');
        document.getElementById('error-message').textContent = msg;
    }

    // --- Retry ---
    document.getElementById('btn-retry').addEventListener('click', () => {
        resetToIdle();
    });

    // --- Reset ---
    function resetToIdle() {
        stateIdle.style.display = '';
        stateProgress.classList.remove('visible');
        stateSuccess.classList.remove('visible');
        stateError.classList.remove('visible');
        clearFile();

        // Reset segments
        segments.forEach(s => {
            s.classList.remove('filled', 'filling');
        });

        // Reset phases
        phases.forEach(p => {
            document.getElementById('phase-' + p).classList.remove('active', 'completed');
        });
    }

    // --- Utility ---
    function formatSize(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
    }

})();
