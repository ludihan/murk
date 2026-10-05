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
        // GitHub's push webhook (https://jenkins.ludihan.com/github-webhook/) triggers builds instantly...
        githubPush()
        // ...and this is the safety net if a webhook delivery is missed.
        pollSCM('H/10 * * * *')
    }
    stages {
        stage('Deploy') {
            steps {
                // Caddy keeps serving while the new build is swapped in underneath it (see docker-compose.yml)
                sh 'docker compose up -d --build'
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
