if (sessionStorage.getItem("darkmode") === 'true') {
    document.documentElement.setAttribute('data-theme', 'dark');
    toggleSwitch.toggleAttribute("checked");
}