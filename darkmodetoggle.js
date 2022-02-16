const toggleSwitch = document.querySelector('.theme-switch input[type="checkbox"]');

if (sessionStorage.getItem("darkmode")) {
    document.documentElement.setAttribute('data-theme', 'dark');
}

function switchTheme(e) {
    if (e.target.checked) {
        document.documentElement.setAttribute('data-theme', 'dark');
        sessionStorage.setItem("darkmode", true);
    }
    else {
        document.documentElement.setAttribute('data-theme', 'light');
        sessionStorage.setItem("darkmode", false);
    }
}

toggleSwitch.addEventListener('change', switchTheme);
