// Rebuild and restart the site on this server whenever main changes.
// Jenkins runs on the same host (see ci/docker-compose.jenkins.yml) and talks to the host's Docker daemon.
pipeline {
    agent any
    options {
        disableConcurrentBuilds()
        timeout(time: 30, unit: 'MINUTES')
        buildDiscarder(logRotator(numToKeepStr: '20'))
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
                // -p murk keeps the compose project (and its caddy_data volume with the HTTPS cert) stable;
                // --wait fails the build if the new container doesn't become healthy
                sh 'docker compose -p murk up -d --build --wait'
            }
        }
    }
    post {
        always {
            // dangling images, plus build cache older than a week
            sh 'docker image prune -f && docker builder prune -f --filter until=168h'
        }
    }
}
