function calcDate() {
	var today = new Date();
	// Updates these on new releases
	// months count from 0 = Jan, 11 = Dec
	var last_release = new Date(2020,05,05);
	// getTime returns in milliseconds, so convert to days
	var diff = Math.floor((today.getTime() - last_release.getTime()) / (1000 * 60 * 60 * 24));

	var years = Math.floor(diff/365.25);
	diff = diff % 365.25;
	var months = Math.floor(diff/30.41666666);
	diff = diff % 30.41666666;
	var weeks = Math.floor(diff/7);
	if (weeks >= 4) {
		weeks -= 4;
		months++;
	}
	diff = diff % 7
	var days = Math.floor(diff);

	var message = "";
	if (years)
		message += years + " Years, ";
	if (months)
		message += months + " Months, ";
	if (weeks)
		message += weeks + " Weeks, ";
	
	message += days + " Days";

	document.getElementById('diffdate').innerHTML = message;

	return
}

window.onload = calcDate;
