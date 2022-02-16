const toggleSwitch = document.querySelector('.theme-switch input[type="checkbox"]');

if (sessionStorage.getItem("darkmode") === 'true') {
    document.documentElement.setAttribute('data-theme', 'dark');
    toggleSwitch.toggleAttribute("checked");
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
