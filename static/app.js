const stepMs = 1500;
let allRows = [];
let shownRows = [];
let currentIndex = 0;

const percentTick = (value) => `${Number(value).toFixed(2)}%`;

const commonOptions = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: { legend: { labels: { color: '#edf2ff' } } },
  scales: {
    x: { ticks: { color: '#cfd6ff', maxRotation: 0, autoSkip: true }, grid: { color: 'rgba(255,255,255,0.08)' } },
    y: { ticks: { color: '#cfd6ff' }, grid: { color: 'rgba(255,255,255,0.08)' } },
  },
};

const qilChart = new Chart(document.getElementById('qilChart'), {
  type: 'line',
  data: { labels: [], datasets: [
    { label: 'Расход измеренный', data: [], borderColor: '#00e5ff', backgroundColor: 'rgba(0,229,255,0.2)', tension: 0.3 },
    { label: 'Расход предсказанный', data: [], borderColor: '#ff70d0', backgroundColor: 'rgba(255,112,208,0.2)', tension: 0.3 },
  ] },
  options: commonOptions,
});

const mapeChart = new Chart(document.getElementById('mapeChart'), {
  type: 'line',
  data: { labels: [], datasets: [{ label: 'Относительная погрешность (%)', data: [], borderColor: '#ffd166', backgroundColor: 'rgba(255,209,102,0.2)', tension: 0.3 }] },
  options: {
    ...commonOptions,
    scales: {
      ...commonOptions.scales,
      y: {
        ...commonOptions.scales.y,
        ticks: {
          ...commonOptions.scales.y.ticks,
          callback: percentTick,
        },
      },
    },
  },
});

const maeChart = new Chart(document.getElementById('maeChart'), {
  type: 'line',
  data: { labels: [], datasets: [{ label: 'Абсолютная погрешность', data: [], borderColor: '#7bf1a8', backgroundColor: 'rgba(123,241,168,0.2)', tension: 0.3 }] },
  options: commonOptions,
});

function updateSignal(meanMapePercent) {
  const card = document.getElementById('metricsCard');
  if (meanMapePercent > 3) {
    card.classList.add('alert');
  } else {
    card.classList.remove('alert');
  }
}

function updateDashboard() {
  if (currentIndex >= allRows.length) return;

  shownRows.push(allRows[currentIndex]);
  currentIndex += 1;

  const labels = shownRows.map((r) => r.Date);
  qilChart.data.labels = labels;
  qilChart.data.datasets[0].data = shownRows.map((r) => r.Q_IL);
  qilChart.data.datasets[1].data = shownRows.map((r) => r.Q_IL_pred);
  qilChart.update();

  mapeChart.data.labels = labels;
  mapeChart.data.datasets[0].data = shownRows.map((r) => Number(r.mape) * 100);
  mapeChart.update();

  maeChart.data.labels = labels;
  maeChart.data.datasets[0].data = shownRows.map((r) => r.mae);
  maeChart.update();

  const last = shownRows[shownRows.length - 1];
  const meanMapePercent = Number(last.mean_mape) * 100;

  document.getElementById('meanMaeCell').textContent = Number(last.mean_mae).toFixed(4);
  document.getElementById('meanMapeCell').textContent = `${meanMapePercent.toFixed(2)}%`;
  document.getElementById('meanQilPredCell').textContent = Number(last.mean_Q_IL_pred).toFixed(4);

  updateSignal(meanMapePercent);
}

async function init() {
  const resp = await fetch('/api/data');
  allRows = await resp.json();
  if (!Array.isArray(allRows) || allRows.length === 0) {
    alert('Положи файл data.xlsx рядом с app.py.');
    return;
  }
  setInterval(updateDashboard, stepMs);
}

init();
