let interval;

// Dark mode toggle
document.addEventListener('DOMContentLoaded', () => {
    const darkSwitch = document.getElementById('darkSwitch');
    if (darkSwitch) {
        if (localStorage.getItem("dark-mode") === "true") {
            document.body.classList.add('dark');
            darkSwitch.checked = true;
        }
        darkSwitch.addEventListener('change', () => {
            document.body.classList.toggle('dark');
            localStorage.setItem("dark-mode", darkSwitch.checked);
        });
    }

    // Form submission for hardness and fruit pages
    const testForm = document.getElementById('testForm');
    if (testForm) {
        testForm.onsubmit = async function(e) {
            e.preventDefault();
            const classificationType = window.location.pathname === '/hardness' ? 'soft_hard' : 'fresh_rotten';
            const cycles = document.getElementById('cycles').value;
            const duration = document.getElementById('duration').value;
            const softThreshold = document.getElementById('softThreshold')?.value || 350;
            const freshThreshold = document.getElementById('freshThreshold')?.value || 750;

            const statusBox = document.getElementById('status');
            statusBox.className = 'status running';
            statusBox.innerHTML = `
                <p>Status: Starting test...</p>
                <p id="timer">Elapsed Time: 0s</p>
                <p id="average" style="display: none;">Average: 0</p>
                <p id="result" style="display: none;">Classification: <span id="result-text"></span></p>
            `;
            document.getElementById('plotArea').style.display = 'none';

            const startBtn = document.getElementById('startBtn');
            startBtn.disabled = true;

            try {
                const res = await fetch('/api/start', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        classification_type: classificationType,
                        cycles: parseInt(cycles),
                        duration: parseInt(duration),
                        soft_threshold: parseInt(softThreshold),
                        fresh_threshold: parseInt(freshThreshold)
                    })
                });

                if (!res.ok) {
                    console.error('Start test failed:', res.status, await res.text());
                    statusBox.className = 'status error';
                    statusBox.innerHTML = '<p>Status: Failed to start test</p>';
                    startBtn.disabled = false;
                    return;
                }

                startBtn.disabled = false;
                clearInterval(interval);
                interval = setInterval(getStatus, 1000);
            } catch (error) {
                console.error('Error starting test:', error);
                statusBox.className = 'status error';
                statusBox.innerHTML = '<p>Status: Error starting test</p>';
                startBtn.disabled = false;
            }
        };
    }

    // Stop test
    const stopBtn = document.getElementById('stopBtn');
    if (stopBtn) {
        stopBtn.onclick = async function() {
            const confirmStop = confirm("Are you sure you want to stop the test?");
            if (!confirmStop) return;

            try {
                const res = await fetch('/api/stop', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' }
                });
                if (!res.ok) {
                    console.error('Stop test failed:', res.status, await res.text());
                }
                clearInterval(interval);
                const statusBox = document.getElementById('status');
                statusBox.className = 'status error';
                statusBox.innerHTML = `
                    <p>Status: Test stopped by user.</p>
                    <p id="timer">Elapsed Time: 0s</p>
                    <p id="average" style="display: none;">Average: 0</p>
                    <p id="result" style="display: none;">Classification: <span id="result-text"></span></p>
                `;
            } catch (error) {
                console.error('Error stopping test:', error);
            }
        };
    }

    // Update status on form pages
    if (window.location.pathname === '/hardness' || window.location.pathname === '/fruit') {
        interval = setInterval(getStatus, 1000);
    }
});

async function getStatus() {
    try {
        const res = await fetch('/api/status');
        if (!res.ok) {
            console.error('Status fetch failed:', res.status, await res.text());
            return;
        }

        const data = await res.json();
        const statusBox = document.getElementById('status');
        if (statusBox) {
            statusBox.className = `status ${data.finished ? (data.status.includes("stopped") ? "error" : "success") : "running"}`;
            statusBox.innerHTML = `
                <p>Status: ${data.status}</p>
                <p id="timer">Elapsed Time: ${data.elapsed_time}s</p>
                <p id="average" ${data.average ? '' : 'style="display: none;"'}>Average: ${data.average ? data.average.toFixed(2) : 0}</p>
                <p id="result" ${data.result ? '' : 'style="display: none;"'}>Classification: <span id="result-text">${data.result}</span></p>
            `;
        }

        if (data.finished) {
            clearInterval(interval);
            const plotArea = document.getElementById('plotArea');
            if (plotArea) {
                plotArea.style.display = 'block';
                const plotImg = document.getElementById('plotImg');
                plotImg.src = `/api/plot_img?t=${new Date().getTime()}`;
                plotImg.onerror = () => {
                    console.error('Failed to load plot image');
                    plotArea.innerHTML = '<p>Error: Unable to load plot. Check server logs.</p>';
                };
                plotImg.onload = () => {
                    console.log('Plot image loaded successfully');
                };
            }
        }
    } catch (error) {
        console.error('Error fetching status:', error);
    }
}