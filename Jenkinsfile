// Rebuild and restart the site on this server whenever main changes.
// Jenkins runs on the same host (see ci/docker-compose.jenkins.yml) and talks to the host's Docker daemon.
pipeline {
    agent any
    options {
        disableConcurrentBuilds()
        timeout(time: 30, unit: 'MINUTES')
    }
    triggers {
        // GitHub's push webhook (https://murk.ludihan.xyz/github-webhook/) triggers builds instantly...
        githubPush()
        // ...and this is the safety net if a webhook delivery is missed.
        pollSCM('H/10 * * * *')
    }
    stages {
        stage('Deploy') {
            steps {
                sh 'docker network inspect murk-web >/dev/null 2>&1 || docker network create murk-web'
                // -p murk keeps the compose project (and its caddy_data volume with the HTTPS cert) stable
                sh 'docker compose -p murk up -d --build'
            }
        }
        stage('Cleanup') {
            steps {
                sh 'docker image prune -f'
            }
        }
    }
}
