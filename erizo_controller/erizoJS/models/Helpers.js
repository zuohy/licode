/*global exports*/
'use strict';

exports.getMediaConfiguration = (mediaConfiguration = 'default') => {
  if (global.mediaConfig && global.mediaConfig.codecConfigurations) {
    if (global.mediaConfig.codecConfigurations[mediaConfiguration]) {
      return JSON.stringify(global.mediaConfig.codecConfigurations[mediaConfiguration]);
    } else if (global.mediaConfig.codecConfigurations.default) {
      return JSON.stringify(global.mediaConfig.codecConfigurations.default);
    } else {
      return JSON.stringify({});
    }
  } else {
    return JSON.stringify({});
  }
};
