const stepMs = 1500;
let allRows = [];
let shownRows = [];
let currentIndex = 0;

const commonOptions = {
  responsive: true,
  maintainAspectRatio: false,
  plugins: {
    legend: {
      labels: {
        color: '#001f3f',
        font: { size: 13, weight: '700' },
        usePointStyle: true,
        pointStyle: 'circle',
      },
    },
  },
  scales: {
    x: {
      ticks: { color: '#2f4f73', maxRotation: 0, autoSkip: true },
      grid: { color: 'rgba(0, 68, 124, 0.08)' },
    },
    y: {
      ticks: { color: '#2f4f73' },
      grid: { color: 'rgba(0, 68, 124, 0.12)' },
    },
  },
};

const qilChart = new Chart(document.getElementById('qilChart'), {
  type: 'line',
  data: { labels: [], datasets: [
    {
      label: 'Расход измеренный',
      data: [],
      borderColor: '#005596',
      backgroundColor: 'rgba(0, 85, 150, 0.14)',
      pointBackgroundColor: '#005596',
      pointBorderColor: '#ffffff',
      pointRadius: 3,
      borderWidth: 3,
      tension: 0.3,
    },
    {
      label: 'Расход предсказанный',
      data: [],
      borderColor: '#ee3124',
      backgroundColor: 'rgba(238, 49, 36, 0.12)',
      pointBackgroundColor: '#ee3124',
      pointBorderColor: '#ffffff',
      pointRadius: 3,
      borderWidth: 3,
      tension: 0.3,
    },
  ] },
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
