/*jshint esversion: 6 */
var syncLoader = (function() {
    var jsFiles = [];
    var cssFiles = [];
    var head = document.getElementsByTagName('head')[0];

    var loadCssFiles = function(count) {
        count = count || 0;

        return new Promise(function (resolve, reject) {
            var link = document.createElement("link");
            link.rel = "stylesheet";
            link.type = "text/css";
            link.href = cssFiles[count];
            link.onload = function () {
                resolve();
            };
            link.onerror = function () {
                reject(Error());
            };
            head.appendChild(link);
        }).then(() => {
            return ++count === cssFiles.length ? true : loadCssFiles(count);
        });

    };

    var loadScriptFiles = function(count) {
        count = count || 0;

        return new Promise(function (resolve, reject) {
            var script = document.createElement('script');
            script.type = 'text/javascript';
            script.src = jsFiles[count];
            script.onload = function () {
                resolve();
            };
            script.onerror = function () {
                reject(Error());
            };
            head.appendChild(script);
        }).then(() => {
            return ++count === jsFiles.length ? true : loadScriptFiles(count);
        });
    };

    var endsWith = function(str, suffix) {
        if (str === null || suffix === null)
            return false;
        return str.indexOf(suffix, str.length - suffix.length) !== -1;
    };

    var loadFiles = function(files) {
        return new Promise(function(resolve, reject) {
            for (var i = 0; i < files.length; ++i) {
                if (endsWith(files[i], ".css")) {
                    cssFiles.push(files[i]);
                } else if (endsWith(files[i], ".js")) {
                    jsFiles.push(files[i]);
                }
            }
            loadScriptFiles().then(() => loadCssFiles()).then(() => resolve());
        });
    };

    return {'loadFiles': loadFiles};

})();


function createUserConfigForm(myJson) {
    var configForm = document.getElementById("userconfig");
    myJson.groups.forEach(group => {
        var fieldset = document.createElement('fieldset');
        var legend = document.createElement('legend');
        legend.innerHTML = group.title;
        fieldset.appendChild(legend);
        configForm.appendChild(fieldset);

        group.entities.forEach(entity => {
            var label = document.createElement('label');
            label.innerHTML = entity.title;
            fieldset.appendChild(label);

            if (entity.tag && entity.tag === 'select') {
                var select = document.createElement('select');
                select.id = entity.name;
                select.name = entity.name;
                entity.options.forEach(o => {
                    let opt = document.createElement('option');
                    opt.value = o;
                    opt.innerHTML = o;
                    select.appendChild(opt);
                });
                fieldset.appendChild(select);
            } else {
                var hidden;
                var input = document.createElement('input');
                input.id = entity.name;
                input.name = entity.name;
                if (entity.attributes) {
                    Object.entries(entity.attributes).forEach(([attr, val]) => {
                        input[attr] = val;
                        // special case for checkboxes
                        // we want checkboxes to be submitted as either 0 or 1 depending on checkstate
                        // since unchecked checkboxes aren't submitted at all, we use a hidden input for that checkbox value
                        if (attr === 'type' && val == 'checkbox') {
                            input.id = entity.name + '_checkbox';
                            input.name = '';
                            hidden = document.createElement('input');
                            hidden.type = 'hidden';
                            hidden.id = entity.name;
                            hidden.name = entity.name;
                            input.onclick = function() {
                                document.getElementById(entity.name).value = this.checked ? 1 : 0;
                            };
                        }
                    });
                }
                fieldset.appendChild(input);
                if (hidden !== undefined) {
                    fieldset.insertBefore(hidden, input);
                }
            }
        });
    });
}
